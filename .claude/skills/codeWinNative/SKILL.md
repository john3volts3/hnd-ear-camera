---
name: codeWinNative
description: Directives de developpement obligatoires pour applications Windows natives C++ — MSVC Build Tools, CMake, COM/WRL, Media Foundation, cameras virtuelles, DLL systeme, installateurs autonomes, droits administrateur
user-invocable: true
---

# Directives de developpement — Applications Windows natives (C++)

Ces directives s'appliquent aux projets C++ natifs Windows : executables Win32, DLL COM, composants Media Foundation (cameras virtuelles), outils en ligne de commande, installateurs autonomes.

---

## 1. Modifications chirurgicales

- Ne modifier que le code **strictement necessaire** pour repondre a la demande.
- Ne JAMAIS supprimer, modifier, deplacer ou refactorer du code non directement concerne.
- Preserver integralement le comportement existant du reste du code.
- En cas de doute, demander confirmation avant d'agir.

## 2. Workflow obligatoire

1. Analyser la demande et le code existant.
2. **Rechercher les API Windows** avant d'implementer : documentation Microsoft Learn, echantillons officiels (`microsoft/Windows-classic-samples`, `microsoft/Windows-Camera`...), version minimale de Windows requise par chaque API.
3. Proposer un **PLAN D'ACTION** detaille (fichiers, modifications, impacts/risques, droits requis).
4. **ATTENDRE la confirmation explicite** ("ok", "go", "valide") avant d'appliquer.
5. Ne jamais appliquer de modifications sans accord prealable.

## 3. Journal des modifications

- Maintenir `JOURNAL.md` a la racine du projet.
- Consigner chaque fichier modifie avec : nom, date/heure, description.
- Section `## Session du AAAA-MM-JJ` pour chaque session.

## 4. Langue

- Communiquer en **francais**.
- Code, commentaires, logs, messages de commit : **anglais**.
- Libelles a l'ecran (dialogues, messages d'erreur) : **anglais** par defaut.

## 5. Chaine de compilation

- **Visual Studio Build Tools 2022** (MSVC) + **Windows SDK** + **CMake** fourni avec les Build Tools. Pas d'IDE requis.
- Localiser les outils avec `vswhere.exe` (`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`), ne jamais coder un chemin d'installation en dur.
- Compiler depuis un environnement initialise par `vcvars64.bat` (script `build.cmd` du projet), generateur CMake `NMake Makefiles` ou `Ninja` si present.
- Options par defaut : `/W4 /permissive- /utf-8 /EHsc /std:c++20`, Release en `/O2`, **CRT statique** (`/MT`) pour que les binaires distribues n'exigent pas le redistribuable VC++.
- Cible **x64** uniquement sauf demande contraire.
- Dossier de build dans le projet (`build/`) et ignore par git. Si la compilation sur le lecteur reseau (P:) est trop lente ou pose des problemes de verrous, utiliser un dossier de build local (`%LOCALAPPDATA%\<projet>\build`).

## 6. Dependances

- **Preferer ce que fournit le SDK Windows** : WRL (`wrl/client.h`, `wrl/implements.h`), WIC, Media Foundation, Winsock, C++/WinRT (en-tetes du SDK).
- WIL, vcpkg, NuGet ou toute bibliotheque tierce : **uniquement avec l'accord de l'utilisateur** (cf. regles de securite des dependances du CLAUDE.md global).
- Code repris d'un exemple open source : verifier la licence (MIT/BSD/Apache), conserver la mention de copyright dans un fichier `THIRD_PARTY_NOTICES.md`.

## 7. Structure type d'un projet

```
projet/
  CMakeLists.txt       # Projet CMake racine
  build.cmd            # Initialise vcvars64 + configure + compile
  src/                 # Sources (un sous-dossier par binaire : dll/, setup/, tools/...)
  tools/               # Scripts de test / diagnostic
  dist/                # Paquets a distribuer (ignore par git)
  README.md / README.fr.md
  FSD.md / PRD.md / JOURNAL.md
  THIRD_PARTY_NOTICES.md
```

## 8. COM, DLL et services Windows

- Utiliser `Microsoft::WRL::ComPtr` et `RuntimeClass` pour les objets COM ; pas de `AddRef/Release` manuels.
- Une DLL chargee par un **service** (ex. Frame Server pour les cameras virtuelles, compte *Local Service*) :
  - doit etre installee dans un dossier lisible par tous : `%ProgramFiles%\<Produit>\` — **jamais** sur un lecteur reseau mappe (P:), invisible pour les services, ni dans le profil utilisateur ;
  - doit etre enregistree dans **HKLM** (droits administrateur) ;
  - ne doit jamais afficher d'interface ni bloquer : journaliser au lieu d'afficher.
- Retirer la **Mark of the Web** des fichiers telecharges avant enregistrement (`Unblock-File`).
- Tout enregistrement (registre, camera virtuelle, service) doit avoir sa **desinstallation symetrique** testee.

## 9. Media Foundation / cameras virtuelles

- `MFCreateVirtualCamera` = **Windows 11 uniquement** (build 22000+) : verifier la version au demarrage de l'installateur et refuser proprement sinon.
- Seules les cameras Media Foundation apparaissent dans l'app **Camera** de Windows ; les cameras virtuelles DirectShow (OBS, pyvirtualcam) n'y apparaissent pas.
- Fournir au moins **NV12** (format prefere) et RGB32.
- Persistance : `MFVirtualCameraLifetime_System` + `MFVirtualCameraAccess_AllUsers` (admin) pour une camera qui survit aux redemarrages ; `Session` pour les tests.
- Reference : `microsoft/Windows-Camera/Samples/VirtualCamera`, `smourier/VCamSample`.

## 10. Reseau (Winsock)

- Lier les sockets a l'**adresse de l'interface cible** quand plusieurs interfaces existent (ex. Wi-Fi d'un appareil + Ethernet du reseau local), pour ne jamais emettre sur le mauvais reseau.
- Verifier l'identite de l'appareil (reponse de decouverte, adresse MAC) avant de lui envoyer des commandes.
- Timeouts systematiques, reconnexion automatique, aucune boucle bloquante sans condition de sortie.

## 11. Droits administrateur et UAC

- Un installateur declare `requireAdministrator` dans son manifeste ; les autres executables restent `asInvoker`.
- Expliquer a l'utilisateur **avant** toute action necessitant l'elevation ce qui sera ecrit (fichiers, cles HKLM) et comment le retirer.

## 12. Distribution et installateur

- Privilegier un **installateur autonome** (un seul `.exe` embarquant les binaires en ressources, CRT statique) plutot qu'un outil tiers (Inno Setup, WiX) sauf accord de l'utilisateur.
- L'installateur gere : installation, mise a jour (ecrase proprement), desinstallation (`/uninstall` + entree dans « Applications installees »).
- Distribution par messagerie (WhatsApp, e-mail) : livrer un `.zip`, et prevenir que sans signature de code **SmartScreen** affiche « Windows a protege votre PC » (« Informations complementaires » puis « Executer quand meme »).
- Numero de version dans les ressources `VERSIONINFO` de chaque binaire.

## 13. Diagnostic

- Journalisation par `OutputDebugStringW` (visible avec DebugView) ou fichier journal dans `%ProgramData%\<Produit>\` pour les composants charges par des services.
- Tester les DLL de service avec un petit executable hote avant l'installation systeme.
- Codes `HRESULT` : toujours journaliser la valeur hexadecimale et le message (`FormatMessageW`).

## 14. Chemins et donnees personnelles

- Aucun chemin absolu personnel (`C:\Users\<nom>`, `P:\...`), adresse IP de reseau local ou e-mail dans les fichiers suivis par git ; utiliser des variables (`%ProgramFiles%`, `%LOCALAPPDATA%`).
- Toujours utiliser le **lecteur mappe** (P:) au lieu des chemins UNC pour travailler sur le NAS.

## 15. Git sur partage reseau (NAS)

- Enchainer `git add` et `git commit` dans le meme appel PowerShell (risque de `index.lock`).
- `build/` et `dist/` dans `.gitignore`.

## 16. Livraison

Quand l'utilisateur demande une **livraison** :

1. Mettre a jour `JOURNAL.md` avec les modifications de la session
2. Mettre a jour `FSD.md` pour refleter les changements fonctionnels
3. Mettre a jour `PRD.md` (exigences, statuts, historique des livraisons)
4. Mettre a jour `README.md` / `README.fr.md` et le manuel utilisateur si applicable
5. Incrementer la version (`VERSIONINFO`), recompiler en Release, produire le `.zip` de distribution dans `dist/`
6. Copier ce skill dans le repository (`.claude/skills/codeWinNative/SKILL.md`) si des directives ont evolue
7. Commit et push (methode PowerShell pour NAS)
