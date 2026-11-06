"""Single-worker adaptive queue, exact resume, conditional study and hard morning shutdown."""
import argparse
from datetime import datetime,timezone
import ctypes
import json
import os
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256
from dru_resume_policy import action
from load_control import LoadMonitor
from scheduled_comparison_results import assess


class Scheduler:
    def __init__(self,manifest,output):
        self.root=Path(__file__).resolve().parents[2];self.build=self.root/'build-patch_neural_restoration'
        if not all(p.resolve().is_relative_to(self.build.resolve()) for p in (manifest,output)):raise ValueError('Owned scheduler paths required')
        self.config=json.loads(manifest.read_text(encoding='utf-8'));self.output=output;output.mkdir(exist_ok=False)
        self.gpu=self.root.parent/'venv-neural-cuda/Scripts/python.exe';self.cpu=self.root/'.venv-neural/Scripts/python.exe'
        self.execution_path=Path(self.config['execution_path']);self.execution=json.loads(self.execution_path.read_text(encoding='utf-8'))
        self.deadline=datetime.fromisoformat(self.execution['hard_stop_utc']).timestamp()
        self.training_deadline=datetime.fromisoformat(self.execution['training_deadline_utc']).timestamp()
        self.monitor=LoadMonitor();self.serial=0
        self.result=dict(schema_version=1,status='initializing',pid=os.getpid(),started_at_utc=datetime.now(timezone.utc).isoformat(),
            manifest_sha256=sha256(manifest),execution=self.execution,stages=[],assessments=[])

    def write(self):
        temp=self.output/'progress.tmp';temp.write_text(json.dumps(self.result,indent=2)+'\n',encoding='utf-8');temp.replace(self.output/'progress.json')

    def check(self):
        if time.time()>=self.deadline:raise TimeoutError('Hard morning shutdown')
        if (self.output/'STOP').exists():raise InterruptedError('User requested scheduler stop')
        if any(sha256(self.root/'tests/neural'/n)!=h for n,h in self.execution['runtime_tools'].items()):raise ValueError('Scheduled source changed')

    def admit(self,cooldown=0,wait_start=True):
        stable=None;earliest=time.time()+cooldown
        if wait_start:earliest=max(earliest,datetime.fromisoformat(self.config['heavy_start_utc']).timestamp())
        while True:
            self.check()
            if time.time()>=self.training_deadline:raise TimeoutError('Morning training budget exhausted')
            sample=self.monitor.sample();self.result['load']=sample;self.result['status']='waiting_for_load';self.write()
            cpu=sample['cpu'];ready=(time.time()>=earliest and sample['memory']['system_used_fraction']<.70
                and cpu is not None and cpu['external_cpu_fraction']<.65)
            if ready:
                if stable is None:stable=time.monotonic()
                if time.monotonic()-stable>=60:return
            else:stable=None
            time.sleep(10)

    def command(self,label,python,script,*args):
        self.check();self.serial+=1
        row=dict(label=label,started_at_utc=datetime.now(timezone.utc).isoformat(),
            command=[str(python),str(self.root/'tests/neural'/script),*map(str,args)])
        self.result['stages'].append(row);self.result['status']=label;self.write()
        with (self.output/f'{self.serial:03d}-{label}.log').open('x',encoding='utf-8') as log:
            child=subprocess.Popen(row['command'],cwd=self.root,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            row['pid']=child.pid;self.write()
            try:
                while True:
                    self.check()
                    try:code=child.wait(timeout=min(30.,max(.1,self.deadline-time.time())));break
                    except subprocess.TimeoutExpired:self.write()
            except BaseException:
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=False)
                if child.poll() is None:child.kill()
                child.wait(timeout=15);raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());self.write();return code

    def job(self,plan_path,folder,seed,execution_path):
        plan=json.loads(plan_path.read_text(encoding='utf-8'));yields=0;faults=0
        while True:
            self.check();mode=action(folder,sha256(plan_path),plan['steps_per_seed'])
            if mode=='reuse':
                self.result['stages'].append(dict(label=f'reuse-{plan["data_arm"]}-{seed}',sha256=sha256(folder/'result.json')));self.write();return
            self.admit(cooldown=300 if yields or faults else 0)
            args=['--plan',plan_path,'--output',folder,'--seed',seed,'--execution',execution_path]
            if mode=='resume':args.append('--resume')
            code=self.command(f'{plan["data_arm"]}-{seed}',self.gpu,'train_dru_cfa.py',*args)
            result=json.loads((folder/'result.json').read_text(encoding='utf-8'))
            self.archive_case(folder,result)
            if code==0 and result['status']=='completed_dru_bootstrap_not_photographic_qualification':return
            if result['status']=='paused_dru_training' and result.get('reason')=='load_scheduler_yield':
                yields+=1
                if yields>24:raise RuntimeError('Bounded load-yield budget exhausted')
                continue
            if result.get('memory',{}).get('reason')=='system_physical_memory_threshold' and faults<2:
                faults+=1;continue
            raise RuntimeError('Worker stopped; no blind retry: '+str(result.get('error',result.get('reason'))))

    def archive_case(self,folder,state):
        trace_path=Path(state['memory_trace']['path'])
        proof=dict(training=state,training_sha256=sha256(folder/'result.json'),
            memory_trace_sha256=sha256(trace_path),memory_trace=[json.loads(r) for r in trace_path.read_text(encoding='utf-8').splitlines()],
            known_controls=[dict(source=c,measurements=json.loads(Path(c['path']).read_text(encoding='utf-8'))) for c in state['controls']])
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        path=self.root/'docs/validation/raw'/f'neural-load-job-v001-{state["data_arm"]}-{state["seed"]}-{stamp}.json'
        with path.open('x',encoding='utf-8') as stream:json.dump(proof,stream,indent=2,allow_nan=False)

    def run_stage(self,name,plans,folders,execution_path):
        for arm in plans:
            folders[arm].mkdir(parents=True,exist_ok=True)
            for seed in (71,73):self.job(plans[arm],folders[arm]/f'seed{seed}',seed,execution_path)
        step=json.loads(next(iter(plans.values())).read_text(encoding='utf-8'))['steps_per_seed']
        result=assess(folders,step);path=self.output/f'assessment-{name}.json'
        with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        self.result['assessments'].append(dict(stage=name,path=str(path.resolve()),sha256=sha256(path),assessment=result));self.write()
        return result

    def continuation(self,label,parent_plans,parent_folders,arms,extra):
        base=self.output/label;base.mkdir(exist_ok=False);plans={};folders={};allowed=[]
        for arm in ['reference',*arms]:
            plan=json.loads(parent_plans[arm].read_text(encoding='utf-8'));start=plan['steps_per_seed']
            parents=[]
            for seed in (71,73):
                state=json.loads((parent_folders[arm]/f'seed{seed}/result.json').read_text(encoding='utf-8'))
                ref=next(r for r in state['milestones'] if Path(r['path']).name=='weights.pt' and r['step']==start)
                if sha256(Path(ref['path']))!=ref['sha256']:raise ValueError('Continuation parent changed')
                parents.append(dict(seed=seed,path=ref['path'],sha256=ref['sha256'],scientific_plan_sha256=sha256(parent_plans[arm])))
            plan.update(parent_checkpoints=parents,steps_per_seed=start+extra,
                milestones=sorted({start,start+min(256,extra),start+min(1024,extra),start+extra}),
                purpose='Matched continuation, all eligible arms plus reference, not production or photo validation',
                training_deadline_utc=self.execution['training_deadline_utc'],campaign_deadline_utc=self.execution['hard_stop_utc'])
            path=base/(arm+'-plan.json');path.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
            plans[arm]=path;folders[arm]=base/arm;allowed.append(sha256(path))
        execution=dict(self.execution,scientific_plan_sha256=allowed)
        path=base/'execution.json';path.write_text(json.dumps(execution,indent=2)+'\n',encoding='utf-8')
        return plans,folders,path

    def run(self):
        power=ctypes.WinDLL('kernel32',use_last_error=True)
        power.SetThreadExecutionState.argtypes=[ctypes.c_uint];power.SetThreadExecutionState.restype=ctypes.c_uint
        if not power.SetThreadExecutionState(0x80000001):raise ctypes.WinError(ctypes.get_last_error())
        try:
            plans={r['name']:Path(r['path']) for r in self.config['arms']}
            for r in self.config['arms']:
                if sha256(Path(r['path']))!=r['sha256']:raise ValueError('Scientific arm changed')
            # Small qualification may run before the user leaves; all substantive jobs wait for14h.
            self.admit(wait_start=False)
            probe=self.output/'execution-qualification'
            if self.command('execution-qualification',self.gpu,'train_dru_cfa.py','--plan',plans['reference'],
                    '--output',probe,'--seed',71,'--preflight','--execution',self.execution_path):raise RuntimeError('Execution preflight failed')
            parity=self.output/'execution-parity.json'
            if self.command('execution-parity',self.gpu,'check_dru_execution_parity.py','--prior',self.config['prior_checkpoint'],
                    '--current',probe/'resume.pt','--output',parity):raise RuntimeError('Numerical resource amendment parity failed')
            self.result['parity']=json.loads(parity.read_text(encoding='utf-8'));self.write()
            folders={a:Path(self.config['comparison_folder'])/a for a in plans}
            for arm in ('noise','details'):
                self.admit()
                probe=self.output/(arm+'-preflight')
                if self.command(arm+'-preflight',self.gpu,'train_dru_cfa.py','--plan',plans[arm],'--output',probe,
                        '--seed',71,'--preflight','--execution',self.execution_path):raise RuntimeError('Data arm preflight failed')
            baseline=self.run_stage('comparison256',plans,folders,self.execution_path)
            strict_arms=baseline['strict_candidates']
            if not strict_arms and baseline['promising_candidates']:
                plans,folders,execution=self.continuation('pilot1024',plans,folders,baseline['promising_candidates'],768)
                pilot=self.run_stage('pilot1024',plans,folders,execution);strict_arms=pilot['strict_candidates']
            if strict_arms:
                plans,folders,execution=self.continuation('bounded-night',plans,folders,strict_arms,2048)
                self.run_stage('bounded-night',plans,folders,execution)
                self.result['status']='completed_scheduler_with_bounded_night_study'
            else:self.result['status']='completed_scheduler_no_fidelity_candidate_for_night'
        except Exception as error:
            self.result.update(status='stopped_on_error_or_load_deadline',error=str(error));raise
        finally:
            power.SetThreadExecutionState(0x80000000)
            self.result['finished_at_utc']=datetime.now(timezone.utc).isoformat();self.write()
            stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
            with (self.root/'docs/validation/raw'/f'neural-load-scheduler-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
                json.dump(self.result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();Scheduler(a.manifest,a.output).run()
