# Journal des modifications

## Session du 2026-11-12

### Diagnostic (sans modification de fichier)
- La « caméra USB » est en réalité une caméra d'oreille **Wi-Fi** HND-NE3 (puce Beken BK7231) : elle crée un réseau ouvert `HNDEC_xx-xxxxxx`, se place en 192.168.1.1 et distribue les adresses par DHCP.
- Aucun port TCP ; balayage UDP par ICMP « port inaccessible » : port 46526 (découverte, fiche JSON), 52219 (accéléromètre), 44506 (vidéo JPEG).
- Protocole confirmé grâce au projet open source NE3-Scope (variante NE7, `client2.py`) puis mesuré : flux 480×480 JPEG à 12-14 images/s, pas de maintien nécessaire, angle de l'accéléromètre étalonné avec l'utilisateur.

### Fichiers créés
| Heure | Fichier | Description |
|---|---|---|
| 19:25 | `src/common/HndCam.h` | Constantes produit : version, noms, CLSID, format de sortie |
| 19:26 | `src/core/Log.h`, `src/core/Log.cpp` | Journalisation (débogueur, fichier plafonné à 1 Mo, console) |
| 19:27 | `src/core/ImageOps.h`, `src/core/ImageOps.cpp` | Décodage JPEG (WIC), rotation bilinéaire + masque circulaire, écran d'état (Direct2D/DirectWrite), conversion NV12/RGB32 |
| 19:28 | `src/core/CameraClient.h`, `src/core/CameraClient.cpp` | Protocole caméra : découverte, démarrage vidéo/IMU, réassemblage JPEG, calcul de l'orientation, reconnexion |
| 19:29 | `CMakeLists.txt`, `build.cmd` | Projet CMake (MSVC Build Tools, CRT statique, /W4) et script de compilation |
| 19:29 | `tools/probe/main.cpp` | `HndCamProbe` : outil console de test (captures PNG) |
| 19:33 | `tools/viewer/hnd_viewer.py`, `tools/viewer/HND_Viewer.cmd` | Visualiseur navigateur en Python (bibliothèque standard uniquement) |
| 19:35 | `src/source/*` | DLL `HndCamSource` : source Media Foundation de la caméra virtuelle (Activator, MediaSource, MediaStream) |
| 19:41 | `src/setup/*` | `HndCamSetup` : installateur autonome (DLL embarquée, enregistrement HKLM, caméra virtuelle, tâche de restauration au démarrage, désinstallation) |
| 19:47 | `tools/viewer/viewer.html` | Page web commune aux deux visualiseurs (rotation, orientation manuelle, photo) |
| 19:52 | `src/viewer/*` | `HndViewer.exe` : visualiseur navigateur autonome en C++ (mini serveur HTTP local) |
| 19:58 | `src/core/WifiConnector.h`, `src/core/WifiConnector.cpp` | Connexion automatique au réseau Wi-Fi d'une caméra (`HNDEC_…`, profil temporaire, mode streaming) |
| 20:13 | `tools/viewer/READ-ME_viewer.txt`, `.gitignore` | Notice anglais/français du paquet du visualiseur, exclusions git |

### Corrections pendant les tests
| Heure | Fichier | Description |
|---|---|---|
| 19:44 | `CameraClient.cpp`, `hnd_viewer.py` | Rotation corrigée de +90° (référence d'étalonnage imprimée verticalement) |
| 19:50 | `hnd_viewer.py` | Arrêt automatique 5 s après la fermeture de la page ; la recherche ignore les erreurs ICMP |
| 20:09 | `CameraClient.cpp`, `WifiConnector.cpp`, `hnd_viewer.py` | Mode streaming Wi-Fi (supprime les gels toutes les 5 s dus aux recherches Wi-Fi de Windows) ; une micro-coupure garde la dernière image au lieu d'afficher l'écran d'attente |
| 20:15 | `Log.cpp`, `src/viewer/main.cpp` | Journal `%TEMP%\HndViewer.log` pour le diagnostic |
| 20:24 | `viewer.html`, `src/viewer/main.cpp` | Le navigateur ferme un flux vidéo resté sans données : la page le rouvre quand la vidéo reprend |

### Environnement Claude (hors dépôt)
- Nouveau skill `/codeWinNative` (`~/.claude/skills/codeWinNative/SKILL.md`), copie dans `.claude/skills/codeWinNative/SKILL.md`.
- `CLAUDE.md` global : ajout de l'environnement « Windows natif C++ » (sauvegarde préalable dans un dossier de sauvegarde local).

### Tests utilisateur (visualiseur `HndViewer.exe`)
- Étape 1, app lancée puis caméra allumée : OK.
- Étape 2, rotation automatique, boutons d'orientation, photo : OK.
- Étape 3, caméra éteinte puis rallumée pendant l'utilisation : OK après correction de 20:24.
- Étape 4, fermeture de l'onglet → arrêt et libération du Wi-Fi : OK.
- Étape 5, caméra allumée puis app lancée : OK.
- **Caméra virtuelle (DLL + installateur) : compilée, pas encore installée ni testée.**
