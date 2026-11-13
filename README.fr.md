# CAM-Oreille : caméra d'oreille HND sur Windows

[English version](README.md)

Utiliser une **caméra d'oreille Wi-Fi HND** (HND-NE3, réseau Wi-Fi nommé `HNDEC_...`, normalement utilisée avec l'application Android « HND ») sur un PC Windows.

| Outil | Rôle | Prérequis |
|---|---|---|
| **CAM-Oreille.exe** | Image en direct dans le navigateur, rotation automatique, photos ; s'installe dans `C:\CAM-OREILLE` avec une icône sur le bureau | Windows 10/11, pas de droits administrateur (sauf pour allumer une carte Wi-Fi) |
| HndCamSetup.exe *(en cours)* | Ajoute « HND Ear Camera » comme webcam (app Caméra, Teams, Zoom, navigateurs) | Windows 11, droits administrateur une fois |

L'application s'affiche en français quand Windows est affiché en français ou utilise des formats régionaux français, en anglais sinon.

## Installation
1. Récupérez `CAM-Oreille-<version>.zip` (voir [Partager avec des amis](#partager-avec-des-amis)) et double-cliquez sur `CAM-Oreille.exe` qu'il contient (inutile d'extraire le zip).
2. Windows affiche « Windows a protégé votre PC » : cliquez sur **Informations complémentaires**, puis **Exécuter quand même** (le programme n'est pas signé).
3. Cliquez sur **Installer et démarrer** : le programme est copié dans `C:\CAM-OREILLE` (lecteur système), une icône **CAM-Oreille** est ajoutée sur le bureau et une entrée dans Paramètres > Applications. Aucun droit administrateur n'est nécessaire. **Démarrer sans installer** le lance une seule fois.
4. Les fois suivantes : double-cliquez sur l'icône du bureau (plus d'avertissement de Windows).

- **Mise à jour :** double-cliquez sur un `CAM-Oreille.exe` plus récent : il propose **Mettre à jour et démarrer** (versions installée et nouvelle affichées).
- **Désinstallation :** Paramètres > Applications > Applications installées > CAM-Oreille > Désinstaller (icône du bureau, entrée dans Applications et dossier supprimés).

## Utilisation
1. Allumez la caméra (voyant bleu) et fermez l'application de la caméra sur le téléphone.
2. Double-cliquez sur l'icône **CAM-Oreille** du bureau.
3. **Wi-Fi éteint sur le PC** (carte désactivée dans le Gestionnaire de périphériques, ou bouton Wi-Fi coupé) : une fenêtre explique que la caméra a son propre réseau Wi-Fi et sa propre plage d'adresses (192.168.1.x), et liste les cartes Wi-Fi du PC. Choisissez-en une et cliquez sur **Allumer le Wi-Fi** : Windows demande l'autorisation (droits administrateur). Avec la case « Éteindre de nouveau la carte Wi-Fi à la fermeture de CAM-Oreille » cochée (par défaut), la carte revient à son état d'avant à la fin. Avec **Pas maintenant**, la page affiche un rappel indiquant que le Wi-Fi est éteint.
4. Le navigateur s'ouvre. Le PC rejoint tout seul le Wi-Fi de la caméra et l'image apparaît en quelques secondes. La caméra peut être allumée avant ou après CAM-Oreille.
5. Boutons : **Rotation automatique**, **Orientation** (pas de 90° / 5°, mémorisée), **Enregistrer la photo** (telle qu'affichée). La version est affichée sous le titre.
6. Fermez l'onglet : CAM-Oreille s'arrête et le PC retrouve son Wi-Fi habituel.

Remarques
- La caméra n'accepte qu'un client à la fois (téléphone **ou** PC).
- Pendant l'utilisation, un PC sans câble réseau n'a pas internet : son Wi-Fi est sur le réseau de la caméra.
- Un PC relié par câble garde internet par le câble : pour Windows, le Wi-Fi de la caméra (54 Mbit/s) passe toujours en second.
- Rien n'est enregistré dans les réglages Wi-Fi du PC (profil de connexion temporaire).
- Journal de diagnostic : `%TEMP%\CAM-Oreille.log`.

Une version Python du visualiseur (bibliothèque standard uniquement, outil de test) se trouve dans `tools/viewer` (`HND_Viewer.cmd`).

## Partager avec des amis
Envoyez le paquet `CAM-Oreille-<version>.zip` (programme + notice bilingue) :
- **WhatsApp :** envoyez le `.zip` comme document.
- **E-mail :** les messageries bloquent les fichiers `.exe`, même dans un `.zip` : envoyez plutôt un **lien de téléchargement** (Google Drive ou la page « Releases » de GitHub).
- N'envoyez pas de script à exécuter (`.cmd`, `.ps1`, commande à coller dans PowerShell) : c'est bloqué par les messageries et c'est la méthode des arnaques.

## Compilation
Prérequis : Visual Studio Build Tools 2022 (charge de travail C++ x64, SDK Windows, CMake).

```
build.cmd
```

Résultat dans `build\bin` : `CAM-Oreille.exe`, `HndCamProbe.exe` (outil de test en console), `HndCamSource.dll`, `HndCamSetup.exe`. Le paquet contient `CAM-Oreille.exe` + `tools/viewer/READ-ME_viewer.txt` (sous le nom `READ-ME - LISEZ-MOI.txt`, UTF-8 avec BOM) dans un `.zip`.

## Fonctionnement
La caméra utilise un protocole UDP propriétaire (découverte sur le port 46526, capteur de mouvement sur 52219, vidéo JPEG sur 44506), décrit dans [FSD.md](FSD.md). Remerciements : [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
