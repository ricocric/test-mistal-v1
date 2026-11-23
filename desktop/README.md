# Sketch3D natif — transcription C++ de la version web

Transcription du prototype web (`index.html` a la racine du depot) en programme
natif Windows, par petits lots testables. Modele geometrique en **double
precision** natif (la version web stockait en Float32).

## Lot N3 (celui-ci)

Scene d'objets (boites) maillage indexe en doubles : selection au clic,
deplacement (fluide, verrouillable X/Y/Z touches tenues),
**import/export OBJ et STL** natifs.

- **I** : importer (*.obj, *.stl) — boite de dialogue Windows ; pose au sol,
  anti-superposition (decalage juste apres le bord de l'objet en collision,
  marge 0.25, annonce en console), cadrage immediat sur l'objet importe,
  invariants affiches en console (volume signe, aretes non-manifold) ;
- **O** : exporter la scene en OBJ (coordonnees monde, un bloc `o` par objet) ;
- **S** : exporter en STL binaire (normales calculees, coords monde) ;
- Selection : clic gauche sur un objet (console : nom + tris) ; glisser :
  deplace sur plan face a la camera ; touches **X/Y/Z** tenues : axe verrouille
  (le verrouillage marche sur AZERTY et QWERTY) ; double-clic : rapprochement ;
- **F** : cadrage (selection, sinon boite englobante de la scene) ; **H** :
  vue d'origine ; clic dans le vide : deselection + orbite ; **Echap** : quitter ;
- la molette : zoom.

Fichier de test conseille : le cube SketchUp deja utilise en web sert de reference
(600+ tris, volume et coque verifiees a l'import).

## Lot N3.1 (correctifs retour de test)

- **Anticollision a l'import** : transcription fidele du web (`placeWithoutOverlap`)
  — l'objet importe est decale en X juste apres le bord de la boite englobante
  en collision + 0.25, jusqu'a un emplacement libre ; le nombre de decalages est
  affiche en console ;
- **Cadrage post-import** : la cible camera est calculee en coordonnees MONDE
  (le facteur d'echelle par objet manquait : la camera visait un point situe a
  des milliers d'unites du vrai centre pour un fichier en millimetres) et la
  distance reprend la formule tangente du cadrage de scene (vue lisible sans
  devoir presser H).

## Lots precedents

- **N1** : fenetre OpenGL + camera orbitale + grille + cube (eprouvette du build).
- **N2** : selection au clic + deplacement, verrouillage d'axe X/Y/Z, cadrage
  (transcription des lots web 2 et 2.1).

## Prerequis

1. **Visual Studio Community 2022 ou 2026** avec la charge de travail
   **« Developpement Desktop avec C++ »** (CMake est inclu dedans).
   Les deux versions conviennent ; les binaires GLFW precompiles (lib-vc2022)
   restent compatibles avec elles.
2. **GLFW 3** (la seule bibliotheque externe du lot N1). Deux facons :
   - Deja installe (vcpkg, MSYS2...) : rien a faire de special.
   - Sinon : telecharger le binaire Windows sur https://www.glfw.org/download.html
     (fichier `glfw-3.5.1.bin.WIN64.zip` ou plus recent),
     dezipper par exemple dans `C:\libs\`.

## Compiler (invite « Developer PowerShell for VS ... »)

Le plus simple est de **ne pas preciser le generateur** : CMake choisit tout
seul la derniere version de Visual Studio installee (2022 ou 2026 ; VS 2026
embarque un CMake assez recent pour la gerer).

```
cd desktop
cmake -B build -DGLFW_HOME=C:/libs/glfw-3.5.1.bin.WIN64
cmake --build build
```

Le chemin `GLFW_HOME` est optionnel si GLFW est deja visible du systeme.
Si besoin d etre explicite : `-G "Visual Studio 17 2022"` (VS 2022) ou
`-G "Visual Studio 18 2026"` (VS 2026, demande CMake 4.2+).
Variante : ouvrir simplement le dossier `desktop` dans Visual Studio (CMake integre).

## Executer

```
build\Debug\sketch3d.exe
```

## Organiseur des prochains lots

- N4 : push/pull sur imports + soudure de sommets (invariants portés du web :
  coque fermee, aretes ×2, NaN, anti-penetration) + batterie de tests native
- N5 : interface (Dear ImGui), cotes, unites, menu Fichier
- ensuite : pipeline par shaders (PBR) pour le rendu photorealiste
- a planifier : fichier de setup utilisateur (regles d import comme la
  normalisation d'echelle, unites par defaut, chemins recents...)

## Notes techniques du lot N1

- **GLFW** pour la fenetre + contexte OpenGL 2.1 (compat).
- **OpenGL fixed-function 1.1** pour le rendu (grille, cube, lumiere fixe) :
  zero chargement d extensions, convenu comme point de depart ; le passage aux
  shaders fera l objet d un lot dedie.
- **Pas de GLU** : matrices perspective (colonne-major) et lookAt calculees a la
  main en doubles.
- Camera orbitale : `dist`, `theta` (azimut), `phi` (elevation bornee 0.02..3.12),
  position en doubles, conversion float seulement au moment de l appel OpenGL.
