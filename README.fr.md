# HND Ear Camera pour Windows

[English version](README.md)

Utiliser une **caméra d'oreille Wi-Fi HND** (HND-NE3, réseau Wi-Fi nommé `HNDEC_...`, normalement utilisée avec l'application Android « HND ») sur un PC Windows.

| Outil | Rôle | Prérequis |
|---|---|---|
| **HndViewer.exe** | Image en direct dans le navigateur, rotation automatique, photos | Windows 10/11, rien à installer, pas de droits administrateur |
| HndCamSetup.exe *(en cours)* | Ajoute « HND Ear Camera » comme webcam (app Caméra, Teams, Zoom, navigateurs) | Windows 11, droits administrateur une fois |

## Visualiseur : démarrage rapide
1. Allumez la caméra (voyant bleu) et fermez l'application de la caméra sur le téléphone.
2. Double-cliquez sur `HndViewer.exe`. Au premier lancement, Windows peut afficher « Windows a protégé votre PC » : cliquez sur **Informations complémentaires**, puis **Exécuter quand même** (le programme n'est pas signé).
3. Le navigateur s'ouvre. Le PC rejoint tout seul le Wi-Fi de la caméra et l'image apparaît en quelques secondes. La caméra peut être allumée avant ou après le visualiseur.
4. Boutons : **Auto-rotate** (rotation automatique), **Orientation** (pas de 90° / 5°, mémorisée), **Save photo** (photo enregistrée telle qu'affichée).
5. Fermez l'onglet : le visualiseur s'arrête et le PC retrouve son Wi-Fi habituel.

Remarques
- La caméra n'accepte qu'un client à la fois (téléphone **ou** PC).
- Pendant l'utilisation, un PC sans câble réseau n'a pas internet : son Wi-Fi est sur le réseau de la caméra.
- Rien n'est enregistré dans les réglages Wi-Fi du PC (profil de connexion temporaire).
- Journal de diagnostic : `%TEMP%\HndViewer.log`.

Une version Python du visualiseur (bibliothèque standard uniquement) se trouve dans `tools/viewer` (`HND_Viewer.cmd`).

## Compilation
Prérequis : Visual Studio Build Tools 2022 (charge de travail C++ x64, SDK Windows, CMake).

```
build.cmd
```

Résultat dans `build\bin` : `HndViewer.exe`, `HndCamProbe.exe` (outil de test en console), `HndCamSource.dll`, `HndCamSetup.exe`.

## Fonctionnement
La caméra utilise un protocole UDP propriétaire (découverte sur le port 46526, capteur de mouvement sur 52219, vidéo JPEG sur 44506), décrit dans [FSD.md](FSD.md). Remerciements : [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
