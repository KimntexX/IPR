"""Build artifact only: one fail-closed post-estimation hook, no maintained filter fork.

The generated header hashes normalized UTF-8/LF text. Canonical study proofs
separately preserve the SHA256 of original on-disk source bytes (often CRLF).
"""
import argparse
import hashlib
from pathlib import Path


MARKER = '        bool measured = false;\n'
HOOK = '''        // GENERATED BENCH HOOK: the original filtering implementation follows unchanged.
        {
            static thread_local std::array<Noise, Levels> benchSaved;
            static thread_local std::array<bool, Levels> benchAvailable{};
            const auto benchOperation = IprNoiseBench::operation();
            if (benchOperation == IprNoiseBench::Operation::CapturePooled) {
                for (int i = 0; i < activeLevels; ++i) {
                    benchSaved[std::size_t(i)] = models[std::size_t(i)].pooled;
                    benchAvailable[std::size_t(i)] = models[std::size_t(i)].pooledValid;
                }
            } else if (benchOperation == IprNoiseBench::Operation::FrozenPooled) {
                for (int i = 0; i < activeLevels; ++i) {
                    if (!benchAvailable[std::size_t(i)]) return false;
                    models[std::size_t(i)] = {};
                    models[std::size_t(i)].pooled = benchSaved[std::size_t(i)];
                    models[std::size_t(i)].pooledValid = true;
                }
            } else if (benchOperation == IprNoiseBench::Operation::PooledBlind) {
                for (auto &model : models) model.valid = false;
            } else if (benchOperation == IprNoiseBench::Operation::ExternalCovariance) {
                const auto &covariances = IprNoiseBench::covariances();
                for (int i = 0; i < activeLevels; ++i) {
                    cv::Matx33f covariance;
                    std::copy(covariances[std::size_t(i)].begin(), covariances[std::size_t(i)].end(), covariance.val);
                    models[std::size_t(i)] = {};
                    if (!fromCovariance(covariance, models[std::size_t(i)].pooled)) return false;
                    models[std::size_t(i)].pooledValid = true;
                }
            } else if (benchOperation == IprNoiseBench::Operation::Zero) {
                models = {};
            }
        }
'''


def instrument(source):
    if source.count(MARKER) != 1 or 'IprNoiseBench' in source:
        raise ValueError('Engine source no longer matches the unique bench boundary')
    return ('// Generated from SHA256 '+hashlib.sha256(source.encode('utf-8')).hexdigest()+'\n'
            '#include "noise_bench_control.h"\n'+source.replace(MARKER, HOOK+MARKER))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.output.parent.name != 'oracle-generated':
        raise ValueError('Only dedicated generated artifact output allowed')
    generated = instrument(args.source.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
