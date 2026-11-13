# PRD — HND Ear Camera pour Windows

## 1. Contexte et problème
Les caméras d'oreille / loupes Wi-Fi HND (modèle HND-NE3, réseau Wi-Fi `HNDEC_…`) ne fonctionnent qu'avec l'application Android « HND ». Elles utilisent un protocole UDP propriétaire : Windows ne les reconnaît pas comme webcam, et aucun logiciel PC n'est fourni.

## 2. Utilisateurs et usages
- Le propriétaire de la caméra : voir l'image sur son PC, prendre des photos, l'utiliser comme webcam.
- Un ami possédant la même caméra : recevoir le programme par messagerie (WhatsApp) et l'utiliser sans compétence technique.

## 3. Objectifs et critères de succès
- Voir l'image de la caméra sur un PC Windows, sans le téléphone, en moins de 20 s après l'allumage.
- Image redressée automatiquement grâce à l'accéléromètre de la caméra.
- Utiliser la caméra dans l'app Caméra de Windows, Teams, Zoom, les navigateurs (caméra virtuelle) : mis de côté le 2026-11-13.
- Distribution simple : un `.zip` par messagerie, sans installation pour le visualiseur.

## 4. Périmètre
**Inclus** : visualiseur dans le navigateur (exe autonome + version Python), connexion Wi-Fi automatique, allumage de la carte Wi-Fi depuis l'app, caméra virtuelle Windows 11, installateur autonome.
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
| EF-09 | Visualiseur autonome `.exe` sans Python, sans installation, sans droits administrateur (depuis 1.1.0 : droits demandés seulement pour allumer une carte Wi-Fi) | indispensable | livrée en 1.0.0 |
| EF-10 | Caméra virtuelle « HND Ear Camera » visible dans l'app Caméra, Teams, Zoom, navigateurs | souhaitable (était indispensable) | mise de côté le 2026-11-13 (développée, jamais installée ni testée) |
| EF-11 | Écran d'attente avec instructions dans la caméra virtuelle quand la caméra n'est pas connectée | important | mise de côté le 2026-11-13 (développée, non testée) |
| EF-12 | Installateur autonome (un `.exe`), désinstallation, entrée dans « Applications installées » | souhaitable (était indispensable) | mise de côté le 2026-11-13 (développé, non testé) |
| EF-13 | Recréation automatique de la caméra virtuelle après redémarrage du PC | important | mise de côté le 2026-11-13 (développée, non testée) |
| EF-14 | Wi-Fi éteint au lancement (carte désactivée ou bouton Wi-Fi coupé) : liste des cartes Wi-Fi à cocher, explication (la caméra a son propre réseau et sa propre plage d'adresses), allumage de la carte choisie avec l'autorisation administrateur, sans passer par le Gestionnaire de périphériques | important | livrée en 1.1.0 |
| EF-15 | Remettre la carte Wi-Fi dans son état d'avant à la fermeture (case cochée par défaut) | important | livrée en 1.1.0 |
| EF-16 | Rappel dans la page quand le Wi-Fi du PC est éteint | important | livrée en 1.1.0 |
| EF-17 | Réglage réseau temporaire de la carte Wi-Fi pendant l'utilisation (métrique haute, pas de DNS de la caméra), rétablissement à la fermeture et tâche de secours au démarrage | souhaitable | proposée (piste future, seulement si un ralentissement est mesuré : cf. NF-08) |
| EF-18 | Nom de l'application « CAM-Oreille » (exe, fenêtres, page, icône) | important | livrée en 1.2.0 |
| EF-19 | Installation au premier lancement dans `C:\CAM-OREILLE` avec icône sur le bureau, sans droits administrateur ; mise à jour ; désinstallation depuis Paramètres > Applications | important | livrée en 1.2.0 |
| EF-20 | Interface en français quand Windows est affiché en français ou utilise des formats régionaux français (anglais par défaut) | important | livrée en 1.2.0 |
| EF-21 | Distribution aux amis : `.zip` par WhatsApp, lien Google Drive, page « Releases » GitHub (dépôt rendu public après vérification des données personnelles) ; pas de script à exécuter | important | livrée en 1.2.0 (paquet `.zip`, dépôt public, release `v1.2.0`) ; lien Google Drive à créer par l'utilisateur s'il le souhaite |
| EF-23 | Version affichée dans la page et dans le titre des fenêtres | souhaitable | livrée en 1.2.0 |
| EF-24 | Vérification automatique des mises à jour au lancement (dernière release GitHub, avant de rejoindre le Wi-Fi de la caméra, ignorée sans internet) et proposition de télécharger et installer la nouvelle version | souhaitable | proposée (version future) |
| EF-22 | Application Android remplaçant l'application « HND » : image, rotation, photo, sans localisation ni compte ni publicité | souhaitable | proposée (projet futur) |

## 6. Exigences non fonctionnelles
| ID | Exigence | Statut |
|---|---|---|
| NF-01 | Aucune dépendance tierce : SDK Windows et bibliothèque standard Python uniquement | respectée |
| NF-02 | Trafic réseau émis uniquement sur l'interface Wi-Fi (jamais sur l'Ethernet / le réseau local) | respectée |
| NF-03 | Rien d'enregistré dans les réglages Wi-Fi du PC (profil temporaire) | respectée |
| NF-04 | Interface en anglais (référence), traduite en français selon Windows depuis 1.2.0 ; documentation en anglais et en français | respectée |
| NF-05 | Exécutables sans redistribuable VC++ (CRT statique), x64 | respectée |
| NF-06 | Page web accessible uniquement depuis le PC (127.0.0.1) | respectée |
| NF-07 | Pas de gel d'image dû aux recherches Wi-Fi de Windows (mode streaming) | respectée |
| NF-08 | Pas de ralentissement du PC quand il est relié à la fois au réseau local (Ethernet) et à la caméra (Wi-Fi) | vérifiée le 2026-11-13 sans modification : internet reste sur le câble (métrique Wi-Fi caméra 55, Ethernet 25), résolutions DNS normales |

## 7. Contraintes et dépendances
- Caméra : un seul client à la fois (déconnecter le téléphone) ; coupe son Wi-Fi pendant la charge.
- Le Wi-Fi du PC ne peut être que sur un réseau : un PC sans Ethernet n'a pas internet pendant l'utilisation.
- Caméra virtuelle : Windows 11 (build 22000+, `MFCreateVirtualCamera`), droits administrateur à l'installation.
- Programmes non signés : avertissement SmartScreen au premier lancement.
- Allumer une carte Wi-Fi désactivée exige les droits administrateur (demande d'autorisation Windows).

## 8. Critères d'acceptation
- EF-01, EF-02, EF-06 : app lancée caméra éteinte, puis caméra allumée → image en moins de 20 s (testé : ≈ 1 s après l'apparition du réseau).
- EF-03 : caméra tournée d'un tour complet sur son axe → image droite.
- EF-07 : caméra éteinte puis rallumée → image de nouveau animée sans action.
- EF-08 : onglet fermé → programme arrêté en ≈ 5 s, Wi-Fi libéré.
- EF-10 : « HND Ear Camera » sélectionnable dans l'app Caméra et affiche l'image.
- EF-13 : après redémarrage du PC, la caméra virtuelle est toujours disponible.
- EF-14 : carte désactivée, app lancée → fenêtre de choix, autorisation, image en moins de 20 s (testé : ≈ 10 s) ; Wi-Fi coupé par le bouton → même chose (testé : ≈ 6 s) ; autorisation refusée → la fenêtre reste ouverte.
- EF-15 : onglet fermé → carte de nouveau désactivée, ou Wi-Fi de nouveau coupé, en moins de 10 s.
- EF-16 : « Not now » → encadré jaune dans la page.
- EF-19 : zip reçu (marqué « venant d'internet ») → avertissement SmartScreen, « Installer et démarrer » → dossier, icône du bureau, entrée Applications, lancement depuis l'icône sans avertissement ; version plus ancienne installée → fenêtre de mise à jour avec les deux versions ; désinstallation → icône, entrée et dossier supprimés (tous testés le 2026-11-13).
- EF-20 : Windows anglais avec formats régionaux fr-BE → fenêtres et page en français (testé).
- EF-23 : « Version 1.2.0 » sous le titre de la page, « CAM-Oreille 1.2.0 » dans le titre des fenêtres (testé).

## 9. Questions ouvertes et décisions
- Décision : visualiseur dans le navigateur (pas de tkinter : décodage JPEG impossible sans Pillow).
- Décision : la caméra virtuelle parle elle-même à la caméra depuis le service Frame Server (pas de programme intermédiaire).
- Décision (2026-11-13) : caméra virtuelle (EF-10 à EF-13) **mise de côté** : CAM-Oreille couvre le besoin (voir l'image, photos) ; elle ne servirait qu'aux appels vidéo et à l'enregistrement. Le code (`src/source`, `src/setup`) reste dans le dépôt et continue d'être compilé ; à reprendre par un test pas à pas (installation, app Caméra, redémarrage, désinstallation) et un renommage « CAM-Oreille Webcam » si le besoin revient.
- Ouvert : la caméra virtuelle, chargée par un service Windows, a-t-elle le droit de changer de réseau Wi-Fi ?
- Ouvert : la caméra propose-t-elle un mode « routeur » (`wifi_mode: AP` dans sa fiche) qui éviterait de changer de réseau Wi-Fi ?
- Ouvert : visualiseur et caméra virtuelle utilisés en même temps sur la même caméra (un seul client accepté).
- Ouvert → mesuré le 2026-11-13 : PC ralenti le 2026-11-12 avec l'Ethernet et le Wi-Fi de la caméra en même temps. Mesures du lendemain (plus de 5 min d'utilisation normale) : aucun ralentissement, internet par le câble, DNS normal. Cause du 2026-11-12 inconnue (probablement plusieurs programmes de test en même temps).
- Décision (2026-11-13) : pas de réglage réseau automatique (EF-17 gardée comme piste future) : il ne corrigerait rien de mesuré et imposerait une autorisation administrateur à chaque lancement.
- Décision : la fenêtre d'allumage du Wi-Fi s'affiche au lancement, avant le navigateur, pour que la demande d'autorisation Windows apparaisse au premier plan.
- Décision : pas de script d'installation envoyé par e-mail (pièces jointes `.exe`/`.cmd`/`.ps1` bloquées par les messageries ; « copier une commande dans PowerShell » est la méthode des arnaques) : `.zip` par WhatsApp ou lien de téléchargement.

## 10. Historique des livraisons
| Date | Version / commit | Exigences livrées |
|---|---|---|
| 2026-11-12 | 1.0.0 (visualiseur), commit `7f7d012` + documentation de livraison | EF-01 à EF-09, NF-01 à NF-07 |
| 2026-11-13 | 1.1.0 (visualiseur), commit `e35fcf6` | EF-14, EF-15, EF-16 ; NF-08 vérifiée |
| 2026-11-13 | 1.2.0 « CAM-Oreille » | EF-18, EF-19, EF-20, EF-23 ; EF-21 en partie (paquet `.zip`) ; correction EF-14 (Wi-Fi coupé avant la désactivation de la carte) |
