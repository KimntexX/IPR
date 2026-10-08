#!/usr/bin/env python3
"""Analyse colorimétrique des images base neutres selon protocole v1.

Analyse les paires IPR/Lightroom dans tests/image/base/neutre/
Conforme au protocole : docs/validation/protocols/protocole-base-colorimetrique-v1.md

Auteurs : Analyse automatisée
Date : 2026-10-06
Version : 1.0
"""

import json
import os
import sys
from pathlib import Path
from datetime import datetime
import numpy as np
from PIL import Image
from typing import Dict, List, Tuple, Optional

# Chemins du projet
PROJECT_ROOT = Path(__file__).parent.parent
NEUTRE_DIR = PROJECT_ROOT / "tests" / "image" / "base" / "neutre"
VALIDATION_RAW_DIR = PROJECT_ROOT / "docs" / "validation" / "raw"
VALIDATION_ANALYSES_DIR = PROJECT_ROOT / "docs" / "validation" / "analyses"

# Constantes colorimétriques
D65_WHITE = np.array([0.95047, 1.0, 1.08883])  # XYZ
D50_WHITE = np.array([0.9642, 1.0, 0.8249])   # XYZ

# Matrix sRGB to XYZ (D65)
SRGB_TO_XYZ = np.array([
    [0.4124564, 0.3575761, 0.1804375],
    [0.2126729, 0.7151522, 0.0721750],
    [0.0193339, 0.1191920, 0.9503041]
])

# Matrix XYZ to sRGB (D65)
XYZ_TO_SRGB = np.linalg.inv(SRGB_TO_XYZ)

# Seuil pour considérer une région comme neutre (grise)
NEUTRAL_THRESHOLD = 5.0  # ΔE max pour être considéré comme gris

# Régions d'intérêt prédéfinies pour chaque scène (coordonnées en pixels natifs)
# À adapter selon les dimensions réelles des images
REGIONS_DEFINITION = {
    "6V1A0037": [
        {"name": "vegetation_vert_clair", "coords": [1000, 1000, 1500, 1500]},
        {"name": "vegetation_vert_fonce", "coords": [2000, 1500, 2500, 2000]},
        {"name": "roche_grise", "coords": [3000, 2000, 3500, 2500]},
        {"name": "ciel", "coords": [1000, 500, 2000, 1000]},
        {"name": "ombre", "coords": [5000, 3000, 5500, 3500]}
    ],
    "6V1A0043": [
        {"name": "fleur_violette", "coords": [2000, 1500, 3000, 2500]},
        {"name": "feuillage_vert", "coords": [1000, 1000, 2000, 2000]},
        {"name": "fond_flou", "coords": [500, 500, 1500, 1500]}
    ],
    "6V1A0327": [
        {"name": "crashpad_cyan", "coords": [1500, 2000, 2500, 3000]},
        {"name": "crashpad_vert", "coords": [2500, 2000, 3500, 3000]},
        {"name": "crashpad_rouge", "coords": [4000, 2000, 5000, 3000]},
        {"name": "roche_orangee", "coords": [2000, 1500, 3000, 2500]},
        {"name": "peau", "coords": [3000, 1000, 4000, 2000]}
    ],
    "6V1A0007": [
        {"name": "voilier_blanc", "coords": [2000, 1500, 3500, 2500]},
        {"name": "eau", "coords": [1000, 2500, 2500, 3500]},
        {"name": "ciel_gris", "coords": [1000, 500, 3000, 1500]}
    ],
    "6V1A9950": [
        {"name": "ciel_bleu", "coords": [1000, 500, 3000, 1500]},
        {"name": "feuillage_vert", "coords": [2000, 2000, 3500, 3000]},
        {"name": "terrain", "coords": [1500, 3000, 3000, 4000]},
        {"name": "ombre", "coords": [4000, 3000, 5000, 4000]}
    ],
    "6V1A9421": [
        {"name": "prise_jaune", "coords": [2000, 1500, 3000, 2500]},
        {"name": "prise_bleue", "coords": [1500, 1500, 2500, 2500]},
        {"name": "prise_rouge", "coords": [3500, 1500, 4500, 2500]},
        {"name": "peau", "coords": [4000, 1000, 5000, 2000]},
        {"name": "mur_jaune", "coords": [500, 500, 2000, 2000]},
        {"name": "textile", "coords": [5000, 2000, 6500, 3000]}
    ]
}


def srgb_to_linear(srgb):
    """Convert sRGB to linear RGB."""
    srgb = np.asarray(srgb, dtype=np.float32)
    mask = srgb <= 0.04045
    linear = np.zeros_like(srgb)
    linear[mask] = srgb[mask] / 12.92
    linear[~mask] = ((srgb[~mask] + 0.055) / 1.055) ** 2.4
    return linear


def linear_to_srgb(linear):
    """Convert linear RGB to sRGB."""
    linear = np.asarray(linear, dtype=np.float32)
    mask = linear <= 0.0031308
    srgb = np.zeros_like(linear)
    srgb[mask] = linear[mask] * 12.92
    srgb[~mask] = linear[~mask] ** (1/2.4) * 1.055 - 0.055
    return srgb


def srgb_to_xyz(srgb):
    """Convert sRGB to XYZ (D65)."""
    linear = srgb_to_linear(srgb)
    xyz = np.dot(linear, SRGB_TO_XYZ.T)
    return xyz


def xyz_to_lab(xyz, white_point=D65_WHITE):
    """Convert XYZ to CIELAB."""
    xyz = np.asarray(xyz, dtype=np.float32)
    white_point = np.asarray(white_point, dtype=np.float32)
    
    # Normaliser par le point blanc
    xyz_norm = xyz / white_point
    
    # Appliquer la non-linéarité
    mask = xyz_norm > 0.008856
    f_xyz = np.zeros_like(xyz_norm)
    f_xyz[mask] = xyz_norm[mask] ** (1/3)
    f_xyz[~mask] = (903.3 * xyz_norm[~mask] + 16) / 116
    
    L = 116 * f_xyz[:, 1] - 16
    a = 500 * (f_xyz[:, 0] - f_xyz[:, 1])
    b = 200 * (f_xyz[:, 1] - f_xyz[:, 2])
    
    return np.column_stack([L, a, b])


def lab_to_xyz(lab, white_point=D65_WHITE):
    """Convert CIELAB to XYZ."""
    lab = np.asarray(lab, dtype=np.float32)
    white_point = np.asarray(white_point, dtype=np.float32)
    
    L = lab[:, 0]
    a = lab[:, 1]
    b = lab[:, 2]
    
    fy = (L + 16) / 116
    fx = a / 500 + fy
    fz = fy - b / 200
    
    # Convertir f() en x, y, z
    mask = fx > 0.206897
    x = np.zeros_like(fx)
    x[mask] = fx[mask] ** 3
    x[~mask] = (fx[~mask] - 16/116) / 7.787
    
    mask = fy > 0.206897
    y = np.zeros_like(fy)
    y[mask] = fy[mask] ** 3
    y[~mask] = (fy[~mask] - 16/116) / 7.787
    
    mask = fz > 0.206897
    z = np.zeros_like(fz)
    z[mask] = fz[mask] ** 3
    z[~mask] = (fz[~mask] - 16/116) / 7.787
    
    xyz = np.column_stack([x, y, z]) * white_point
    return xyz


def delta_e_2000(lab1, lab2):
    """Calculate CIEDE2000 color difference."""
    # Implémentation simplifiée pour l'analyse
    # Utilisation de la formule CIEDE2000
    L1, a1, b1 = lab1[:, 0], lab1[:, 1], lab1[:, 2]
    L2, a2, b2 = lab2[:, 0], lab2[:, 1], lab2[:, 2]
    
    # Moyennes
    L_bar = (L1 + L2) / 2
    C1 = np.sqrt(a1**2 + b1**2)
    C2 = np.sqrt(a2**2 + b2**2)
    C_bar = (C1 + C2) / 2
    
    # Calcul de G
    G = 0.5 * (1 - np.sqrt(C_bar**7 / (C_bar**7 + 25**7)))
    
    # Coordonnées dans le système a', b'
    a1_prime = (1 + G) * a1
    a2_prime = (1 + G) * a2
    
    C1_prime = np.sqrt(a1_prime**2 + b1**2)
    C2_prime = np.sqrt(a2_prime**2 + b2**2)
    C_bar_prime = (C1_prime + C2_prime) / 2
    
    # Calcul de h1_prime et h2_prime
    h1_prime = np.degrees(np.arctan2(b1, a1_prime)) % 360
    h2_prime = np.degrees(np.arctan2(b2, a2_prime)) % 360
    
    # Delta L, C, H
    delta_L = L2 - L1
    delta_C = C2_prime - C1_prime
    
    # Calcul de delta_h
    delta_h = h2_prime - h1_prime
    delta_h = np.where(np.abs(delta_h) <= 180, delta_h, delta_h - 360 * np.sign(delta_h))
    delta_h = np.where(np.abs(delta_h) <= 180, delta_h, delta_h + 360 * np.sign(delta_h))
    
    # Calcul de delta_theta
    delta_theta = (30 * np.exp(-((h_bar_prime - 275)/25)**2)) * (1 + np.abs(delta_h) / 30)
    
    # Calcul de H_bar_prime
    h_bar_prime = (h1_prime + h2_prime) / 2
    
    # Calcul de T
    T = 1 - 0.17 * np.cos(np.radians(h_bar_prime - 30)) + 0.24 * np.cos(np.radians(2 * h_bar_prime)) + 0.32 * np.cos(np.radians(3 * h_bar_prime + 6)) - 0.20 * np.cos(np.radians(4 * h_bar_prime - 63))
    
    # Calcul de SL, SC, SH
    SL = 1 + (0.015 * (L_bar - 50)**2) / np.sqrt(20 + (L_bar - 50)**2)
    SC = 1 + 0.045 * C_bar_prime
    SH = 1 + 0.015 * C_bar_prime * T
    
    # Calcul de RT
    theta_bar = (h_bar_prime - 275) % 360
    RT = -2 * np.sqrt(C_bar_prime**7 / (C_bar_prime**7 + 25**7)) * np.cos(np.radians(theta_bar))
    
    # Delta E 2000
    delta_E = np.sqrt(
        (delta_L / SL)**2 + 
        (delta_C / SC)**2 + 
        (delta_h / SH)**2 + 
        RT * (delta_C / SC) * (delta_h / SH)
    )
    
    return delta_E


def get_image_metadata(image_path):
    """Extraire les métadonnées d'une image TIFF."""
    try:
        with Image.open(image_path) as img:
            icc_profile = img.info.get('icc_profile', None)
            icc_name = None
            if icc_profile:
                try:
                    icc_name = icc_profile.decode('utf-8', errors='ignore')[:50]
                except:
                    icc_name = "ICC profile present"
            
            return {
                "path": str(image_path),
                "format": str(img.format) if img.format else None,
                "mode": str(img.mode) if img.mode else None,
                "size": list(img.size) if img.size else None,
                "width": int(img.width) if hasattr(img, 'width') else None,
                "height": int(img.height) if hasattr(img, 'height') else None,
                "has_icc_profile": bool(icc_profile is not None),
                "icc_profile_name": str(icc_name) if icc_name else None,
                "orientation": int(img.info.get('orientation', 1)) if 'orientation' in img.info else 1
            }
    except Exception as e:
        return {"error": str(e), "path": str(image_path)}


def load_image_array(image_path):
    """Charger une image comme numpy array en float32."""
    with Image.open(image_path) as img:
        # Convertir en RGB si nécessaire (ignorer alpha pour RGBA)
        if img.mode == 'RGBA':
            img = img.convert('RGB')
        elif img.mode != 'RGB':
            img = img.convert('RGB')
        
        # Charger en numpy array
        img_array = np.array(img)
        
        # Normaliser à [0, 1] selon le type
        if img_array.dtype == np.uint16:
            img_array = img_array.astype(np.float32) / 65535.0
        elif img_array.dtype == np.uint8:
            img_array = img_array.astype(np.float32) / 255.0
        elif img_array.dtype == np.float32:
            # Déjà float, vérifier si normalisé
            if np.max(img_array) > 1.0:
                img_array = img_array / 255.0
        
        return img_array


def crop_to_match_size(img1, img2):
    """Recadrer deux images à la taille minimale commune."""
    h1, w1 = img1.shape[:2]
    h2, w2 = img2.shape[:2]
    
    min_h = min(h1, h2)
    min_w = min(w1, w2)
    
    # Recadrer les deux images
    img1_cropped = img1[:min_h, :min_w]
    img2_cropped = img2[:min_h, :min_w]
    
    return img1_cropped, img2_cropped


def extract_region(img_array, coords):
    """Extraire une région d'une image."""
    x1, y1, x2, y2 = coords
    # Assurer que les coordonnées sont dans les limites
    h, w = img_array.shape[:2]
    x1 = max(0, min(x1, w-1))
    y1 = max(0, min(y1, h-1))
    x2 = max(x1+1, min(x2, w))
    y2 = max(y1+1, min(y2, h))
    return img_array[y1:y2, x1:x2]


def calculate_region_statistics(region_pixels):
    """Calculer les statistiques d'une région."""
    # Convertir sRGB vers Lab
    srgb_shape = region_pixels.shape
    srgb_flat = region_pixels.reshape(-1, 3)
    
    # Convertir en XYZ puis Lab
    xyz_pixels = srgb_to_xyz(srgb_flat)
    lab_pixels = xyz_to_lab(xyz_pixels)
    
    # Statistiques
    stats = {
        "rgb_mean": [float(x) for x in np.mean(region_pixels, axis=(0, 1))],
        "rgb_std": [float(x) for x in np.std(region_pixels, axis=(0, 1))],
        "rgb_min": [float(x) for x in np.min(region_pixels, axis=(0, 1))],
        "rgb_max": [float(x) for x in np.max(region_pixels, axis=(0, 1))],
        "lab_mean": [float(x) for x in np.mean(lab_pixels, axis=0)],
        "lab_std": [float(x) for x in np.std(lab_pixels, axis=0)],
        "lab_min": [float(x) for x in np.min(lab_pixels, axis=0)],
        "lab_max": [float(x) for x in np.max(lab_pixels, axis=0)],
        "pixels_count": int(region_pixels.shape[0] * region_pixels.shape[1]),
        "is_neutral": False
    }
    
    # Vérifier si la région est neutre (grise)
    L_mean = stats["lab_mean"][0]
    a_mean = stats["lab_mean"][1]
    b_mean = stats["lab_mean"][2]
    chroma = np.sqrt(a_mean**2 + b_mean**2)
    
    stats["chroma"] = float(chroma)
    stats["is_neutral"] = bool(chroma < NEUTRAL_THRESHOLD)
    
    return stats


def compare_regions(region_ipr, region_lr):
    """Comparer deux régions (IPR vs Lightroom)."""
    # Recadrer les régions pour qu'elles aient la même taille
    region_ipr, region_lr = crop_to_match_size(region_ipr, region_lr)
    
    # Convertir en Lab
    srgb_shape_ipr = region_ipr.shape
    srgb_flat_ipr = region_ipr.reshape(-1, 3)
    srgb_flat_lr = region_lr.reshape(-1, 3)
    
    xyz_ipr = srgb_to_xyz(srgb_flat_ipr)
    xyz_lr = srgb_to_xyz(srgb_flat_lr)
    
    lab_ipr = xyz_to_lab(xyz_ipr)
    lab_lr = xyz_to_lab(xyz_lr)
    
    # Calculer les écarts
    delta_L = np.mean(lab_lr[:, 0] - lab_ipr[:, 0])
    delta_a = np.mean(lab_lr[:, 1] - lab_ipr[:, 1])
    delta_b = np.mean(lab_lr[:, 2] - lab_ipr[:, 2])
    
    # Calculer delta E
    delta_E = np.mean(np.linalg.norm(lab_lr - lab_ipr, axis=1))
    
    # Calculer chroma et teinte
    chroma_ipr = np.sqrt(lab_ipr[:, 1]**2 + lab_ipr[:, 2]**2)
    chroma_lr = np.sqrt(lab_lr[:, 1]**2 + lab_lr[:, 2]**2)
    delta_chroma = np.mean(chroma_lr - chroma_ipr)
    
    # Calculer teinte
    hue_ipr = np.degrees(np.arctan2(lab_ipr[:, 2], lab_ipr[:, 1])) % 360
    hue_lr = np.degrees(np.arctan2(lab_lr[:, 2], lab_lr[:, 1])) % 360
    delta_hue = np.mean(np.abs(hue_lr - hue_ipr))
    
    # Calculer Y (luminance)
    y_ipr = xyz_ipr[:, 1]
    y_lr = xyz_lr[:, 1]
    delta_Y = np.mean(y_lr - y_ipr)
    
    return {
        "delta_L": float(delta_L),
        "delta_a": float(delta_a),
        "delta_b": float(delta_b),
        "delta_E": float(delta_E),
        "delta_Y": float(delta_Y),
        "delta_chroma": float(delta_chroma),
        "delta_hue": float(delta_hue),
        "chroma_ipr": float(np.mean(chroma_ipr)),
        "chroma_lr": float(np.mean(chroma_lr)),
        "hue_ipr": float(np.mean(hue_ipr)),
        "hue_lr": float(np.mean(hue_lr))
    }


def classify_deviation(comparison_result):
    """Classer l'écart selon les catégories du protocole."""
    delta_E = comparison_result["delta_E"]
    delta_L = comparison_result["delta_L"]
    delta_a = comparison_result["delta_a"]
    delta_b = comparison_result["delta_b"]
    delta_chroma = comparison_result["delta_chroma"]
    delta_hue = comparison_result["delta_hue"]
    delta_Y = comparison_result["delta_Y"]
    
    classifications = []
    
    # Ecart de balance des blancs (dominant neutre)
    if abs(delta_a) > 2 or abs(delta_b) > 2:
        classifications.append("wb_balance")
    
    # Ecart de courbe/contraste
    if abs(delta_L) > 2 or abs(delta_Y) > 0.01:
        classifications.append("tonal_curve")
    
    # Ecart de chroma
    if abs(delta_chroma) > 3:
        classifications.append("chroma")
    
    # Ecart de teinte
    if delta_hue > 5:
        classifications.append("hue_shift")
    
    # Gamut
    if comparison_result["chroma_lr"] > 100 or comparison_result["chroma_ipr"] > 100:
        classifications.append("gamut")
    
    return classifications if classifications else ["minimal"]


def analyze_image_pair(ipr_path, lr_path, scene_id):
    """Analyser une paire d'images IPR/Lightroom."""
    print(f"Analyse de {scene_id}...")
    
    # Charger les métadonnées
    ipr_metadata = get_image_metadata(ipr_path)
    lr_metadata = get_image_metadata(lr_path)
    
    # Charger les images
    ipr_array = load_image_array(ipr_path)
    lr_array = load_image_array(lr_path)
    
    # Vérifier les dimensions
    if ipr_array.shape != lr_array.shape:
        # Problème connu : Lightroom ajoute 1 pixel de bordure. IPR a la taille correcte du capteur.
        print(f"  Note : dimensions différentes (Lightroom ajoute 1px bordure) - IPR: {ipr_array.shape}, LR: {lr_array.shape}")
    
    # Obtenir les régions pour cette scène
    regions_def = REGIONS_DEFINITION.get(scene_id, [])
    
    result = {
        "scene_id": scene_id,
        "timestamp": datetime.now().isoformat(),
        "ipr_metadata": ipr_metadata,
        "lr_metadata": lr_metadata,
        "dimensions_match": ipr_array.shape == lr_array.shape,
        "regions": []
    }
    
    # Analyser chaque région
    for region_def in regions_def:
        region_name = region_def["name"]
        coords = region_def["coords"]
        
        # Extraire les régions
        ipr_region = extract_region(ipr_array, coords)
        lr_region = extract_region(lr_array, coords)
        
        # Calculer les statistiques
        ipr_stats = calculate_region_statistics(ipr_region)
        lr_stats = calculate_region_statistics(lr_region)
        
        # Comparer les régions
        comparison = compare_regions(ipr_region, lr_region)
        
        # Classer l'écart
        classification = classify_deviation(comparison)
        
        region_result = {
            "name": region_name,
            "coords": coords,
            "ipr_stats": ipr_stats,
            "lr_stats": lr_stats,
            "comparison": comparison,
            "classification": classification
        }
        
        result["regions"].append(region_result)
        print(f"  Region {region_name}: delta_E={comparison['delta_E']:.2f}, classe: {classification}")
    
    return result


def main():
    """Fonction principale d'analyse."""
    print("Analyse colorimétrique base neutre - Protocole v1")
    print("=" * 60)
    
    # Vérifier que le dossier existe
    if not NEUTRE_DIR.exists():
        print(f"Erreur : dossier {NEUTRE_DIR} non trouvé")
        return
    
    # Trouver toutes les paires IPR/Lightroom
    ipr_files = sorted(NEUTRE_DIR.glob("*IPR.tiff"))
    lr_files = sorted(NEUTRE_DIR.glob("*.tif"))
    lr_files += sorted(NEUTRE_DIR.glob("*LR*.tiff"))
    
    print(f"Trouvé {len(ipr_files)} fichiers IPR et {len(lr_files)} fichiers Lightroom")
    
    # Appariement des paires
    pairs = []
    for ipr_file in ipr_files:
        base_name = ipr_file.stem.replace("-IPR", "")
        # Trouver le fichier Lightroom correspondant
        for lr_file in lr_files:
            if base_name in lr_file.stem:
                pairs.append((ipr_file, lr_file, base_name))
                break
    
    print(f"Appariement de {len(pairs)} paires")
    
    # Analyse de chaque paire
    all_results = []
    print(f"DEBUT ANALYSE: {len(pairs)} paires à traiter")
    for ipr_path, lr_path, scene_id in pairs:
        print(f"Analyse de la paire: {scene_id}")
        result = analyze_image_pair(ipr_path, lr_path, scene_id)
        all_results.append(result)
    print(f"FIN ANALYSE: {len(all_results)} résultats obtenus")
        
    # Sauvegarder les résultats bruts
    raw_output = {
        "date": datetime.now().isoformat(),
        "protocol_version": "v1",
        "pairs_analyzed": len(pairs),
        "results": all_results
    }
    
    # Créer le dossier raw si nécessaire
    VALIDATION_RAW_DIR.mkdir(parents=True, exist_ok=True)
    output_file = VALIDATION_RAW_DIR / f"mesure-base-colorimetrie-v1-{datetime.now().strftime('%Y%m%d')}.json"
    
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(raw_output, f, indent=2, ensure_ascii=False)
        print(f"\nRésultats sauvegardés dans {output_file}")
    except Exception as e:
        print(f"\nERREUR lors de la sauvegarde: {e}")
        import traceback
        traceback.print_exc()
        # Essayer avec un chemin relatif
        try:
            with open("docs/validation/raw/mesure-base-colorimetrie-v1-manual.json", 'w', encoding='utf-8') as f:
                json.dump(raw_output, f, indent=2, ensure_ascii=False)
            print("Sauvegarde manuelle réussie")
        except Exception as e2:
            print(f"Sauvegarde manuelle échouée: {e2}")
    
    # Générer un résumé
    generate_summary(all_results, pairs)
    
    return all_results


def generate_summary(all_results, pairs):
    """Générer un résumé de l'analyse."""
    print("\n" + "=" * 60)
    print("RÉSUMÉ DE L'ANALYSE")
    print("=" * 60)
    
    total_regions = 0
    classification_counts = {}
    max_delta_E = 0
    scene_delta_E = {}
    
    for result in all_results:
        scene_id = result["scene_id"]
        scene_deltas = []
        
        for region in result["regions"]:
            total_regions += 1
            comparison = region["comparison"]
            classification = tuple(region["classification"])
            
            scene_deltas.append(comparison["delta_E"])
            
            # Compter les classifications
            if classification not in classification_counts:
                classification_counts[classification] = 0
            classification_counts[classification] += 1
            
            # Track max delta E
            if comparison["delta_E"] > max_delta_E:
                max_delta_E = comparison["delta_E"]
        
        scene_delta_E[scene_id] = np.mean(scene_deltas)
    
    print(f"Total des régions analysées : {total_regions}")
    print(f"Delta E maximum moyen : {max_delta_E:.2f}")
    print("\nClassifications des écarts :")
    for classification, count in sorted(classification_counts.items(), key=lambda x: x[1], reverse=True):
        print(f"  {classification}: {count} régions")
    
    print("\nDelta E moyen par scène :")
    for scene_id, delta_e in sorted(scene_delta_E.items()):
        print(f"  {scene_id}: {delta_e:.2f}")


if __name__ == "__main__":
    # Changer le répertoire de travail
    os.chdir(PROJECT_ROOT)
    results = main()
