# PRD — HND Ear Camera pour Windows

## 1. Contexte et problème
Les caméras d'oreille / loupes Wi-Fi HND (modèle HND-NE3, réseau Wi-Fi `HNDEC_…`) ne fonctionnent qu'avec l'application Android « HND ». Elles utilisent un protocole UDP propriétaire : Windows ne les reconnaît pas comme webcam, et aucun logiciel PC n'est fourni.

## 2. Utilisateurs et usages
- Le propriétaire de la caméra : voir l'image sur son PC, prendre des photos, l'utiliser comme webcam.
- Un ami possédant la même caméra : recevoir le programme par messagerie (WhatsApp) et l'utiliser sans compétence technique.

## 3. Objectifs et critères de succès
- Voir l'image de la caméra sur un PC Windows, sans le téléphone, en moins de 20 s après l'allumage.
- Image redressée automatiquement grâce à l'accéléromètre de la caméra.
- Utiliser la caméra dans l'app Caméra de Windows, Teams, Zoom, les navigateurs (caméra virtuelle).
- Distribution simple : un `.zip` par messagerie, sans installation pour le visualiseur.

## 4. Périmètre
**Inclus** : visualiseur dans le navigateur (exe autonome + version Python), connexion Wi-Fi automatique, caméra virtuelle Windows 11, installateur autonome.
**Exclu** : réglages de la caméra (luminosité LED, mot de passe Wi-Fi), mode « routeur » de la caméra, Windows 10 pour la caméra virtuelle, macOS/Linux, plusieurs caméras simultanées, enregistrement vidéo.

## 5. Exigences fonctionnelles
| ID | Exigence | Priorité | Statut |
|---|---|---|---|
| EF-01 | Trouver la caméra sur le réseau Wi-Fi sans adresse fixe (découverte par diffusion) | indispensable | livrée en 1.0.0 (visualiseur) |
| EF-02 | Recevoir et afficher le flux vidéo JPEG (≈ 12 images/s) | indispensable | livrée en 1.0.0 (visualiseur) |
| EF-03 | Rotation automatique de l'image selon l'accéléromètre ; orientation figée quand la caméra pointe vers le haut ou le bas | important | livrée en 1.0.0 (visualiseur) |
| EF-04 | Orientation manuelle (pas de 90° et 5°, mémorisée) et désactivation de la rotation automatique | important | livrée en 1.0.0 (visualiseur) |
| EF-05 | Photo enregistrée telle qu'affichée | important | livrée en 1.0.0 (visualiseur) |
| EF-06 | Connexion automatique au réseau Wi-Fi de n'importe quelle caméra allumée (`HNDEC_…`), quel que soit l'ordre d'allumage | indispensable | livrée en 1.0.0 (visualiseur) |
| EF-07 | Reprise automatique de l'image quand la caméra est éteinte puis rallumée | indispensable | livrée en 1.0.0 (visualiseur) |
| EF-08 | Arrêt automatique du visualiseur à la fermeture de la page et retour au Wi-Fi habituel | important | livrée en 1.0.0 (visualiseur) |
| EF-09 | Visualiseur autonome `.exe` sans Python, sans installation, sans droits administrateur | indispensable | livrée en 1.0.0 |
| EF-10 | Caméra virtuelle « HND Ear Camera » visible dans l'app Caméra, Teams, Zoom, navigateurs | indispensable | développée, à tester |
| EF-11 | Écran d'attente avec instructions dans la caméra virtuelle quand la caméra n'est pas connectée | important | développée, à tester |
| EF-12 | Installateur autonome (un `.exe`), désinstallation, entrée dans « Applications installées » | indispensable | développé, à tester |
| EF-13 | Recréation automatique de la caméra virtuelle après redémarrage du PC | important | développée, à tester |

## 6. Exigences non fonctionnelles
| ID | Exigence | Statut |
|---|---|---|
| NF-01 | Aucune dépendance tierce : SDK Windows et bibliothèque standard Python uniquement | respectée |
| NF-02 | Trafic réseau émis uniquement sur l'interface Wi-Fi (jamais sur l'Ethernet / le réseau local) | respectée |
| NF-03 | Rien d'enregistré dans les réglages Wi-Fi du PC (profil temporaire) | respectée |
| NF-04 | Interface en anglais ; documentation en anglais et en français | respectée |
| NF-05 | Exécutables sans redistribuable VC++ (CRT statique), x64 | respectée |
| NF-06 | Page web accessible uniquement depuis le PC (127.0.0.1) | respectée |
| NF-07 | Pas de gel d'image dû aux recherches Wi-Fi de Windows (mode streaming) | respectée |

## 7. Contraintes et dépendances
- Caméra : un seul client à la fois (déconnecter le téléphone) ; coupe son Wi-Fi pendant la charge.
- Le Wi-Fi du PC ne peut être que sur un réseau : un PC sans Ethernet n'a pas internet pendant l'utilisation.
- Caméra virtuelle : Windows 11 (build 22000+, `MFCreateVirtualCamera`), droits administrateur à l'installation.
- Programmes non signés : avertissement SmartScreen au premier lancement.

## 8. Critères d'acceptation
- EF-01, EF-02, EF-06 : app lancée caméra éteinte, puis caméra allumée → image en moins de 20 s (testé : ≈ 1 s après l'apparition du réseau).
- EF-03 : caméra tournée d'un tour complet sur son axe → image droite.
- EF-07 : caméra éteinte puis rallumée → image de nouveau animée sans action.
- EF-08 : onglet fermé → programme arrêté en ≈ 5 s, Wi-Fi libéré.
- EF-10 : « HND Ear Camera » sélectionnable dans l'app Caméra et affiche l'image.
- EF-13 : après redémarrage du PC, la caméra virtuelle est toujours disponible.

## 9. Questions ouvertes et décisions
- Décision : visualiseur dans le navigateur (pas de tkinter : décodage JPEG impossible sans Pillow).
- Décision : la caméra virtuelle parle elle-même à la caméra depuis le service Frame Server (pas de programme intermédiaire).
- Ouvert : la caméra virtuelle, chargée par un service Windows, a-t-elle le droit de changer de réseau Wi-Fi ?
- Ouvert : la caméra propose-t-elle un mode « routeur » (`wifi_mode: AP` dans sa fiche) qui éviterait de changer de réseau Wi-Fi ?
- Ouvert : visualiseur et caméra virtuelle utilisés en même temps sur la même caméra (un seul client accepté).

## 10. Historique des livraisons
| Date | Version / commit | Exigences livrées |
|---|---|---|
| 2026-11-12 | 1.0.0 (visualiseur) | EF-01 à EF-09, NF-01 à NF-07 |
