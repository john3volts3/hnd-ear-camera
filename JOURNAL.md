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

### Livraison
- 20:40 : commit `7f7d012` (code + documentation), puis livraison 1.0.0 du visualiseur : PRD (historique), JOURNAL, push GitHub, sauvegarde de l'environnement Claude.
- Paquet à partager : `dist/HND-Ear-Camera-Viewer-1.0.0.zip` (non suivi par git, à recréer avec `build.cmd` puis copie de `HndViewer.exe` + `READ-ME_viewer.txt`).

### À faire à la session suivante
1. **PC très ralenti avec l'Ethernet et le Wi-Fi caméra connectés en même temps** (constat de l'utilisateur, redémarrage nécessaire). Pistes : la caméra annonce par DHCP une passerelle et un serveur DNS (port UDP 53 ouvert sur la caméra) sans accès internet ; Windows envoie les requêtes DNS sur toutes les interfaces et peut attendre le DNS de la caméra. Vérifier `Get-DnsClientServerAddress`, les routes et métriques ; envisager de retirer la passerelle / le DNS de l'interface Wi-Fi pendant l'utilisation, ou de passer en adresse fixe sans passerelle.
2. Tester la caméra virtuelle (installation avec UAC, app Caméra, Teams, redémarrage, désinstallation), selon le même protocole pas à pas.
3. Nettoyage : deux profils Wi-Fi manuels de test `HNDEC_…` créés pendant le diagnostic (inutiles, les applications utilisent un profil temporaire) — à supprimer avec l'accord de l'utilisateur.

## Session du 2026-11-13

### Diagnostic du ralentissement Ethernet + Wi-Fi caméra (sans modification de fichier)
- Caméra connectée, visualiseur 1.0.0 en cours, plus de 5 min d'utilisation normale du PC : **aucun ralentissement**.
- La caméra est en 802.11g à 54 Mbit/s : métrique automatique 55 pour son Wi-Fi contre 25 pour l'Ethernet, donc internet passe par le câble.
- Le DNS annoncé par la caméra (192.168.1.1) est bien utilisé par Windows sur l'interface Wi-Fi, mais les résolutions restent normales (12 à 100 ms, bonnes adresses, rien de détourné dans le cache DNS).
- Conclusion : pas de réglage réseau automatique (gardé comme piste future, PRD EF-17). Cause du ralentissement du 2026-11-12 inconnue.

### Fichiers créés
| Heure | Fichier | Description |
|---|---|---|
| 05:19 | `src/viewer/WifiAdapters.h`, `src/viewer/WifiAdapters.cpp` | Liste des cartes Wi-Fi physiques, même désactivées (SetupAPI, clé du pilote) ; assistant administrateur `/wifi-on` : active la carte, allume la radio, les remet dans leur état à la fin du visualiseur |
| 05:24 | `src/viewer/HndViewer.manifest` | Manifeste du visualiseur : Common Controls 6 (TaskDialog), Windows 10/11, DPI par écran |

### Fichiers modifiés
| Heure | Fichier | Description |
|---|---|---|
| 05:18 | `src/core/WifiConnector.h`, `src/core/WifiConnector.cpp` | `IsWifiOn()` (aucune carte activée ou Wi-Fi coupé) et `GetRadioState()` |
| 05:18 | `src/core/CameraClient.h`, `src/core/CameraClient.cpp` | Nouvel état `WifiOff` (nom, message de l'écran d'état) |
| 05:22 | `src/viewer/main.cpp` | Fenêtre de choix de la carte Wi-Fi au lancement (explication de la plage d'adresses de la caméra, case « éteindre à la fermeture »), demande d'autorisation, mode assistant `/wifi-on`, champ `wifiOff` dans `/status.json`, journal factorisé (`InitLog`) |
| 05:24 | `CMakeLists.txt` | `WifiAdapters.cpp`, manifeste, bibliothèques `comctl32`, `setupapi`, `cfgmgr32` ; version 1.1.0 |
| 05:24 | `tools/viewer/viewer.html` | Encadré jaune « Wi-Fi is turned off » |
| 05:45 | `src/common/HndCam.h` | Version 1.1.0 |
| 05:46 | `README.md`, `README.fr.md`, `tools/viewer/READ-ME_viewer.txt` | Allumage du Wi-Fi depuis l'app ; internet conservé par le câble |
| 05:47 | `FSD.md` | §3.1 état `WifiOff`, nouveau §3.4 bis (allumage de la carte Wi-Fi), mesures réseau, limitations |
| 05:48 | `PRD.md` | EF-14 à EF-16 livrées en 1.1.0, EF-17 à EF-22 (réglage réseau futur, renommage CAM-Oreille, installation, français, distribution, app Android), NF-08 vérifiée, décisions, historique |

### Tests utilisateur (caméra réelle, carte Intel AX211)
- Test 1, carte désactivée : fenêtre de choix correcte, autorisation acceptée, image ≈ 10 s après le clic : OK.
- Test 2, fermeture de l'onglet : carte de nouveau désactivée < 1 s après l'arrêt du visualiseur : OK.
- Test 3, « Not now » : encadré jaune dans la page : OK.
- Test 4, autorisation refusée : la fenêtre reste ouverte, puis « Not now » : OK.
- Test 5, carte activée mais Wi-Fi coupé par le bouton : mention « (Wi-Fi turned off) », Wi-Fi rallumé, image ≈ 6 s ; à la fermeture le Wi-Fi est recoupé, la carte reste activée : OK.

### Environnement Claude (hors dépôt)
- Mémoire `pc-network-setup` complétée avec les mesures du jour (sauvegarde préalable dans un dossier de sauvegarde local).

### Livraison
- 1.1.0 du visualiseur : documentation, commit, push, paquet `dist/HND-Ear-Camera-Viewer-1.1.0.zip` (non suivi par git).

### À faire
1. Version 1.2.0 : renommage « CAM-Oreille », installation au premier lancement dans `C:\CAM-OREILLE` + icône sur le bureau + désinstallation, interface française selon la langue de Windows, distribution (WhatsApp, Google Drive, GitHub Releases après vérification des données personnelles du dépôt).
2. Piste future : réglage réseau temporaire (PRD EF-17), seulement si un ralentissement est mesuré.
3. Projet futur : application Android (PRD EF-22), nouveau skill d'environnement à créer.
4. Toujours en attente : test de la caméra virtuelle ; suppression des deux profils Wi-Fi de test `HNDEC_…` (avec l'accord de l'utilisateur).

### Version 1.2.0 « CAM-Oreille » (même session)

#### Fichiers créés
| Heure | Fichier | Description |
|---|---|---|
| 05:50 | `src/viewer/CAM-Oreille.ico` | Icône (anneau bleu de la page autour d'un objectif), 16 à 256 px, générée par un script PowerShell (System.Drawing) |
| 05:51 | `src/viewer/Lang.h` | `UiFrench()` (affichage ou formats régionaux de Windows en français) et `Tr(anglais, français)` |
| 05:52 | `src/viewer/Installer.h`, `src/viewer/Installer.cpp` | Installation dans `%SystemDrive%\CAM-OREILLE` sans droits administrateur, mise à jour (comparaison des versions), raccourci bureau, entrée Paramètres > Applications (HKCU), désinstallation `/uninstall` |

#### Fichiers modifiés
| Heure | Fichier | Description |
|---|---|---|
| 05:51 | `src/common/HndCam.h` | Version 1.2.0 ; nom, exe, dossier, clé de désinstallation et mutex du visualiseur |
| 05:53 | `src/viewer/main.cpp` | Nom CAM-Oreille, textes traduits, champs `text`, `lang`, `version` dans `/status.json`, journal `%TEMP%\CAM-Oreille.log`, `/uninstall`, installation proposée au lancement ; 06:14 : version dans le titre des fenêtres |
| 05:54 | `src/viewer/HndViewer.rc`, `CMakeLists.txt` | Icône, VERSIONINFO « CAM-Oreille », exe `CAM-Oreille.exe` (`OUTPUT_NAME`), `Installer.cpp`, bibliothèque `version` ; version 1.2.0 |
| 05:55 | `tools/viewer/viewer.html` | Titre CAM-Oreille, dictionnaire français, état traduit, consigne corrigée (plus de Ctrl+C), « i/s » ; 06:14 : version sous le titre |
| 05:57 | `tools/viewer/READ-ME_viewer.txt` | Notice bilingue réécrite : installation, utilisation, mise à jour, désinstallation |
| 05:59 | `src/viewer/WifiAdapters.cpp` | Correction : après l'activation d'une carte dont le Wi-Fi était coupé, surveillance de la radio 8 s avant de décider de la rallumer (le pilote annonce d'abord la radio allumée) |
| 06:14 | `src/viewer/Installer.cpp` | Version dans le titre des fenêtres ; journal de la fenêtre proposée (installation / mise à jour depuis quelle version) et de la réponse |
| 06:20 | `README.md`, `README.fr.md`, `FSD.md`, `PRD.md` | CAM-Oreille, installation, partage avec des amis, §3.4 ter / quater du FSD, EF-18 à EF-23 |

#### Tests utilisateur (zip marqué « reçu par WhatsApp » dans Téléchargements)
- Test A, première installation depuis le zip : avertissement SmartScreen, fenêtres en français, dossier + icône + entrée Applications : OK. Défaut trouvé : carte désactivée alors que son Wi-Fi était coupé → Wi-Fi resté coupé (corrigé à 05:59).
- Désinstallation par Paramètres > Applications : icône, entrée et dossier supprimés : OK.
- Test D, réinstallation de la version corrigée : Wi-Fi rallumé tout seul, image, tout en français : OK.
- Test E, lancement depuis l'icône du bureau : aucun avertissement, pas de fenêtre d'installation : OK.
- Test F, mise à jour depuis une fausse 1.1.9 : fenêtre de mise à jour avec les deux versions (confirmé par le journal), version 1.2.0 installée et affichée dans la page : OK.
- Diagnostic annexe : les `.zip` ne s'ouvraient pas au double-clic sur le PC de l'utilisateur, car ils étaient associés à `7z.exe` (7-Zip en ligne de commande) ; réglable par « Ouvrir avec » (Explorateur Windows ou `7zFM.exe`).

#### Livraison
- 1.2.0 : documentation, commit, push, paquet `dist/CAM-Oreille-1.2.0.zip` (non suivi par git).

#### À faire
1. Publication pour les amis : lien Google Drive (par l'utilisateur) ; page « Releases » GitHub après vérification de l'historique et accord explicite pour rendre le dépôt public.
2. Piste future : réglage réseau temporaire (PRD EF-17), seulement si un ralentissement est mesuré.
3. Projet futur : application Android (PRD EF-22), nouveau skill d'environnement à créer.
4. Toujours en attente : test de la caméra virtuelle ; suppression des deux profils Wi-Fi de test `HNDEC_…` (avec l'accord de l'utilisateur).
