# FSD — HND Ear Camera pour Windows

## 1. Vue d'ensemble
| Composant | Fichier | Rôle |
|---|---|---|
| Cœur caméra | `src/core` (bibliothèque statique `hndcore`) | Protocole, réassemblage JPEG, orientation, Wi-Fi, images, journal |
| Visualiseur autonome « CAM-Oreille » | `src/viewer` (cible CMake `HndViewer`) → `CAM-Oreille.exe` | Mini serveur HTTP local + page web ; allumage de la carte Wi-Fi (`WifiAdapters`) ; installation, mise à jour, désinstallation (`Installer`) ; textes français (`Lang.h`) |
| Visualiseur Python | `tools/viewer/hnd_viewer.py` | Même fonction, bibliothèque standard Python (outil de test) |
| Page web | `tools/viewer/viewer.html` | Commune aux deux visualiseurs (embarquée dans l'exe) |
| Caméra virtuelle | `src/source` → `HndCamSource.dll` | Source Media Foundation chargée par le service Frame Server |
| Installateur | `src/setup` → `HndCamSetup.exe` | Installe / désinstalle / restaure la caméra virtuelle |
| Outil de test | `tools/probe` → `HndCamProbe.exe` | Console : état, images/s, captures PNG |

## 2. Protocole de la caméra HND-NE3 (rétro-ingénierie)
- Point d'accès Wi-Fi ouvert `HNDEC_xx-xxxxxx`, caméra en 192.168.1.1, DHCP. Un seul client à la fois. Aucun port TCP.
- **Découverte** : `66 39 01 01` vers le port UDP **46526** (diffusion du sous-réseau). Réponse JSON : `uuid`, `model` (`EAR_CAMERA`), `brand` (`HND-NE3`), `soc` (`BK7231UQFN40`), `firmware`, `bat_capacity`, `macid`, `wifi_mode` (`AP`)…
- **Accéléromètre** : `86 06 01` vers le port **52219** ; la caméra renvoie des paquets de 24 octets (≈ 12 Hz). Octets 0-1, 2-3, 4-5 : axes X, Y, Z sur 12 bits signés (octet fort puis quartet faible) ; octets 18-19 : angle calculé par la caméra (int16 big-endian, = -90 - roulis). X est l'axe de visée.
- **Vidéo** : `20 36` vers le port **44506** ; arrêt : `20 37`. Chaque paquet : `[0]` numéro d'image (8 bits), `[1]` drapeaux (bit 0 = dernier morceau), `[2]` numéro de morceau (à partir de 1), `[3]` inconnu, puis les données. Le dernier morceau se termine par 5 octets à retirer. Les morceaux mis bout à bout forment un JPEG complet 480×480.
- Aucun maintien nécessaire (flux stable plus de 150 s après une seule commande).

## 3. Algorithmes
### 3.1 Recherche et connexion
1. Lister les interfaces Wi-Fi actives (`GetAdaptersAddresses`, type IEEE 802.11) et leur adresse de diffusion. Aucune : état `WifiOff` si aucune carte Wi-Fi n'est activée ou si le Wi-Fi est coupé (`WlanEnumInterfaces` + `wlan_intf_opcode_radio_state`, bouton Wi-Fi, mode avion ou interrupteur), sinon `NoWifi`.
2. Socket UDP **lié à l'adresse Wi-Fi** (rien ne sort par l'Ethernet), envoi de la découverte, attente 1,2 s.
3. Réponse acceptée si `brand` contient « HND » ou `model` contient « EAR_CAMERA ».
4. Sinon, au plus toutes les 6 s : recherche Wi-Fi (`WlanScan`), choix du réseau **ouvert** nommé `HNDEC_…` au meilleur signal, différent du réseau actuel, connexion avec un **profil temporaire** (`WlanConnect`, `wlan_connection_mode_temporary_profile`) : rien n'est enregistré dans Windows.
5. À l'arrêt : déconnexion du réseau de la caméra seulement si le programme l'a rejoint lui-même ; Windows reprend le réseau habituel.

### 3.2 Flux vidéo
- Démarrage : commandes accéléromètre + vidéo, **mode streaming Wi-Fi** activé (`wlan_intf_opcode_media_streaming_mode`) pour supprimer les recherches Wi-Fi d'arrière-plan qui gelaient l'image toutes les 5 s.
- Réassemblage dans 4 emplacements par numéro d'image ; morceaux de plus de 1 s abandonnés.
- Pas d'image depuis 2,5 s : nouvelle commande de démarrage (la dernière image reste affichée). Depuis 7 s : reconnexion complète (retour à 3.1).

### 3.3 Orientation
- roulis = atan2(ay, -az) ; rotation d'affichage (sens horaire) = -90° - roulis (= angle fourni par la caméra), plus le décalage manuel.
- Si la composante de gravité dans le plan de roulis est inférieure à 45 % de g (caméra pointée vers le haut ou le bas), l'orientation précédente est conservée.
- Filtre passe-bas (facteur 0,25) sur le plus court chemin angulaire.

### 3.4 Visualiseurs (navigateur)
- Serveur HTTP sur 127.0.0.1, port 8765 (ou suivant libre jusqu'à 8774 pour l'exe). Routes : `/` page, `/status.json`, `/stream.mjpg` (flux `multipart/x-mixed-replace`), `/snapshot.jpg` (image brute).
- `/status.json` : `state` (clé en anglais lue par la page, identique à la version Python), `text` (état traduit affiché), `camera`, `fps`, `angle`, `frames`, `wifiOff`, `lang` (`fr` / `en`), `version`. La version Python n'envoie ni `text`, ni `lang`, ni `version`, ni `wifiOff` : la page reste alors en anglais, sans version ni encadré.
- `wifiOff` vrai : la page affiche un encadré jaune (Wi-Fi éteint, comment le rallumer). La version Python n'envoie pas ce champ : encadré jamais affiché.
- La page applique la rotation en CSS (cadre rond), propose l'orientation manuelle (mémorisée dans le navigateur) et la photo (canvas, telle qu'affichée).
- Le navigateur ferme un flux MJPEG resté plus de ≈ 7 s sans données : la page rouvre le flux quand l'état repasse à « Streaming », et après une erreur de chargement.
- Arrêt automatique : aucune connexion vidéo et aucune interrogation d'état depuis 5 s (après la première visite). L'exe s'arrête aussi si la page n'a jamais été ouverte au bout de 2 min. Un seul exe à la fois (mutex).
- Journal : `%TEMP%\CAM-Oreille.log` (`%TEMP%\HndViewer.log` jusqu'à 1.1.0).

### 3.4 bis Allumage de la carte Wi-Fi (visualiseur exe, depuis 1.1.0)
- Au lancement, avant l'ouverture du navigateur (le programme a encore le premier plan : la demande d'autorisation Windows s'affiche devant au lieu de clignoter dans la barre des tâches), si aucun Wi-Fi n'est allumé :
  1. Liste des cartes Wi-Fi **physiques**, même désactivées : `SetupDiGetClassDevs` (classe réseau, `DIGCF_PRESENT`), clé du pilote lisible sans droits administrateur : `*IfType` = 71 (IEEE 802.11) et `Characteristics` & `NCF_PHYSICAL` (0x4, exclut Wi-Fi Direct et cartes virtuelles), `NetCfgInstanceId` = GUID de l'interface. État : désactivée (`CM_PROB_DISABLED`), Wi-Fi coupé (radio logicielle), coupé par interrupteur (radio matérielle : non proposée).
  2. Fenêtre `TaskDialogIndirect` : explication (la caméra a son propre réseau Wi-Fi et sa plage d'adresses 192.168.1.x, comparée à celle du réseau actuel du PC lue avec `GetAdaptersAddresses` + passerelle), une case d'option par carte, case « Turn the Wi-Fi adapter off again when the viewer closes » cochée par défaut, bouton « Turn on Wi-Fi » avec bouclier, bouton « Not now ».
  3. « Turn on Wi-Fi » : `ShellExecuteEx` verbe `runas` sur l'exe lui-même : `CAM-Oreille.exe /wifi-on <pid du visualiseur> <0|1> "<identifiant de la carte>"`. Autorisation refusée : la fenêtre reste ouverte.
- Assistant administrateur (`/wifi-on`, invisible, ni page ni mutex) : ouvre le processus du visualiseur, active la carte (`SetupDiCallClassInstaller` `DIF_PROPERTYCHANGE`, `DICS_ENABLE` global puis profil courant, comme le Gestionnaire de périphériques), attend son interface Wi-Fi (15 s max), puis surveille la radio 8 s (une carte désactivée alors que son Wi-Fi était coupé annonce d'abord la radio allumée, puis coupée quand le pilote restaure l'état mémorisé), et allume la radio logicielle si elle est coupée (`WlanSetInterface` `wlan_intf_opcode_radio_state`). Avec 1 : attend la fin du visualiseur, puis recoupe la radio et/ou désactive la carte (`DICS_DISABLE`, profil courant), uniquement ce qu'il a lui-même changé. Si le PC s'éteint pendant l'utilisation, la carte reste simplement allumée.
- Le même journal `%TEMP%\CAM-Oreille.log` reçoit les lignes de l'assistant (préfixe « Wi-Fi helper »).

### 3.4 ter Installation, mise à jour, désinstallation (depuis 1.2.0)
- Dossier : `%SystemDrive%\CAM-OREILLE` (en pratique `C:\CAM-OREILLE`). Créé sans droits administrateur : à la racine du lecteur système, Windows donne aux « Utilisateurs authentifiés » le droit de créer des dossiers.
- Au lancement (après le mutex, avant la fenêtre Wi-Fi), si l'exe lancé n'est pas `C:\CAM-OREILLE\CAM-Oreille.exe` : version installée lue dans ses ressources (`GetFileVersionInfo`).
  - Rien d'installé : fenêtre « Install CAM-Oreille on this PC? » (« Install and start » / « Start without installing »).
  - Version installée plus ancienne : fenêtre « Update CAM-Oreille on this PC? » avec les deux versions (« Update and start » / « Start this copy without updating »).
  - Même version ou plus récente : la copie lancée démarre sans rien demander.
  - Le journal note la fenêtre proposée, la version installée et la réponse.
- Installation : copie de l'exe, suppression de la marque « venant d'internet » (flux `Zone.Identifier`) sur la copie installée (Windows a déjà averti l'utilisateur une fois : l'icône démarre ensuite sans avertissement), raccourci `CAM-Oreille.lnk` sur le bureau (`IShellLink`, `FOLDERID_Desktop`), entrée `HKCU\...\Uninstall\CAM-Oreille` (nom, version, éditeur, icône, `"C:\CAM-OREILLE\CAM-Oreille.exe" /uninstall`, taille). Puis le programme libère le mutex, lance la copie installée (`AllowSetForegroundWindow`) et se termine.
- Désinstallation (`/uninstall`, depuis Paramètres > Applications) : refusée si CAM-Oreille est ouvert (mutex) ; confirmation ; suppression du raccourci et de l'entrée ; puis une commande cachée (`cmd /c ping` 2 s, `del` du seul exe, `rmdir` du dossier seulement s'il est vide) supprime le programme après la fin du processus.

### 3.4 quater Langue et version (depuis 1.2.0)
- Français si la langue d'affichage de Windows (`GetUserDefaultUILanguage`) **ou** les formats régionaux (`GetUserDefaultLCID`) sont en français (cas d'un Windows anglais configuré en Belgique ou en France), anglais sinon. Textes traduits par `Tr(anglais, français)` dans l'exe ; la page reçoit `lang` et applique son dictionnaire français.
- Version affichée dans le titre des fenêtres (« CAM-Oreille 1.2.0 »), sous le titre de la page et dans l'onglet du navigateur.
- Mesures du 2026-11-13 (Ethernet + Wi-Fi de la caméra) : la caméra est en 802.11g à 54 Mbit/s, métrique automatique 55 contre 25 pour l'Ethernet, internet passe donc par le câble ; le DNS de la caméra (192.168.1.1) est connu de Windows mais les résolutions restent normales. Aucun réglage réseau n'est appliqué.

### 3.5 Caméra virtuelle (à tester)
- `HndCamSource.dll` : objet COM `{43B7D877-ACE8-4D4D-9A8D-FA946B7B77AA}` (Activator → MediaSource → MediaStream), un flux vidéo 640×480, NV12 (préféré) et RGB32, 30 i/s annoncées (images répétées), échantillons en mémoire système (gestionnaire D3D ignoré).
- L'image de la caméra est tournée et inscrite dans un disque de 480 px ; sans caméra, un écran d'état explique quoi faire.
- Réglages lus dans `HKLM\Software\HND Ear Camera` : `AutoRotate` (DWORD, 1), `RotationOffset` (DWORD signé, degrés).
- Journal : `%ProgramData%\HND Ear Camera\logs\source.log` (dossier accessible en écriture au compte Local Service).
- `HndCamSetup.exe` (droits administrateur) : vérifie Windows 11, copie la DLL et lui-même dans `%ProgramFiles%\HND Ear Camera`, enregistre le CLSID (HKLM), crée la caméra (`MFCreateVirtualCamera`, durée System, tous les utilisateurs), crée la tâche planifiée « HND Ear Camera - restore » (au démarrage, compte SYSTEM, `/restore`) car la caméra virtuelle ne survit pas au redémarrage, et ajoute l'entrée de désinstallation. Options : `/install`, `/uninstall`, `/restore`, `/quiet`.

## 4. Entrées / sorties
- Entrées : réseau Wi-Fi de la caméra (UDP 46526, 52219, 44506), clics dans la page, choix de la carte Wi-Fi au lancement.
- Sorties : page web locale, photos JPEG téléchargées, caméra virtuelle Windows, journaux, état de la carte Wi-Fi (activée / désactivée, radio) le temps de l'utilisation.

## 5. Limitations
- Une caméra et un client à la fois ; visualiseur et caméra virtuelle ne doivent pas être utilisés en même temps.
- Pendant l'utilisation, le Wi-Fi du PC est sur le réseau de la caméra : pas d'internet sans Ethernet.
- La caméra coupe son Wi-Fi pendant la charge.
- Résolution 480×480, ≈ 12-14 images/s (limite de la caméra).
- Caméra virtuelle : Windows 11 uniquement ; non testée à ce jour.
- Programmes non signés (avertissement SmartScreen, demande d'autorisation « Éditeur inconnu »).
- Allumage du Wi-Fi : demande d'autorisation administrateur ; carte coupée par un interrupteur physique non proposée ; mode avion non testé.
- Quand le Wi-Fi est éteint, le journal note « no wireless interface » ou « WlanGetAvailableNetworkList failed » toutes les 6 s (sans conséquence).
- Installation : un seul utilisateur Windows (raccourci et entrée Applications dans son profil) ; un autre compte du même PC peut lancer `C:\CAM-OREILLE\CAM-Oreille.exe` directement.
- Le nom « CAM-Oreille » ne concerne que le visualiseur : la caméra virtuelle garde le nom « HND Ear Camera ».
