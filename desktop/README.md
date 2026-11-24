# Sketch3D natif — transcription C++ de la version web

Transcription du prototype web (`index.html` a la racine du depot) en programme
natif Windows, par petits lots testables. Modele geometrique en **double
precision** natif (la version web stockait en Float32).

## Outillages T1 (celui-ci) : duplication + rotation

- **Duplication** : menu contextuel (clic droit sur un objet) > **Dupliquer...**
  — nombre d'instances tape dans la fen^etre au clavier puis **Entree** (Entree
  sans chiffre = 1, maximum 999, Echap pour annuler, meme modele de saisie que
  Push/Pull — jamais d'attente bloquante dans la boucle de jeu) ; copies posees
  en ligne vers +X (pas = portee + 0.25) avec la meme anti-superposition que
  l'import : une copie qui rencontre un voisin est poussee au-dela de son bord ;
  noms numerotes depuis le nom source ;
- **Rotation** : touche **R** (bascule du mode, exclusif avec Push/Pull) —
  selectionnez un objet puis **X, Y ou Z** (lettre, AZERTY inclus) pour fixer
  l'axe ; la piece tourne **autour de son centre** en bougeant la souris
  horizontalement (0.25 deg/pixel), ou tapez des **chiffres** (signe autorise,
  virgule ou point) + **Entree** pour un angle exact ; **clic gauche** : pose ;
  **clic droit ou Echap** : annule ; **R** pendant une rotation : annule et
  quitte le mode ;
- la rotation s'applique aux sommets locaux (double precision) : tout le reste
  (deplacement, Push/Pull, exports, fichier projet) marche sur la piece tournee
  sans modification du modele de donnees ;
- la batterie monte a **95 verifications** (familles duplication et rotation).

## Lot N5.1 : menus de base

**Barre de menus Windows** (Fichier) et **menu contextuel du clic droit**,
plus un **fichier de projet** natif.

- **Fichier** : Nouveau projet (scene vide) / Ouvrir un projet (*.s3projet) /
  Enregistrer le projet (*.s3projet) / Fermer le projet / --- / Importer un
  objet (OBJ, STL) / Exporter en OBJ / Exporter en STL / --- / Quitter ;
- **fichier projet `.s3projet`** (texte) : un bloc par objet (nom, position,
  echelle, couleur, sommets locaux, triangles) — la relecture restaure la scene
  telle que sauvegardee (pas de normalisation d'echelle ni de pose au sol) ;
- **menu contextuel (clic droit sur un objet)** : selectionne l'objet, affiche
  ses infos en console et propose **Infos objet** (position, dimensions monde,
  echelle, sommets/tris) ; clic droit dans le vide : rien ; pendant le
  Push/Pull, le clic droit garde son role d'annulation ;
- raccourcis clavier inchanges : **I** import ; O/S : exports OBJ/STL ;
- la batterie monte a **84 verifications** (nouvelle famille projet :
  aller-retour enregistrement/relecture fidele — noms, positions, echelle,
  topologie, sommets a 1e-12 pres — et infos objet).

## Lot N4

**Push/Pull** (extrusion de face), **soudure des sommets a l'import** et
**batterie de tests native** (`desktop/tests/`).

- **P** : bascule le mode Push/Pull — cliquez une face d'un objet (create ou
  importe), **deplacez la souris sans bouton tenu**, la face suit le curseur
  sur son plan ; **second clic gauche** : pose ; **clic droit ou Echap** :
  annule et restaure la piece ;
- pendant le deplacement : tapez des **chiffres** (+ virgule ou point, Retour
  arriere pour corriger) puis **Entree** pour poser une **dimension exacte**
  (toujours positive, comme le web 3.10.3) ;
- **butee** : quand la piece est deterree a zero, la face devient une **feuille**
  (couche unique) ; un nouveau **pull sur la feuille recree le volume** (fond +
  copie translatée + jupe, comme le web) ; le push dans la matiere **plie les
  parois sans jamais penetrer** (repli exact sur le plan) ;
- la transcription est fidele au web 3.10.3 : patch coplanaire connexe (BFS sur
  aretes), parois voisines etirees par indices partages (aucune jupe en double),
  jupe uniquement sur les aretes de bord non partagees ;
- **soudure a l'import** : les sommets coïncidents des OBJ/STL sont fondus
  (tolerance 1e-6 relatif, grille spatiale) et les tris degeneres supprimes —
  message console `import OBJ : N sommets soudes (M restants)` ; sans elle le
  push/pull sur imports en tri-soup deconnait (face entiere = un seul triangle) ;
- **tests** : `desktop/tests/` contient un harnais geometrique sans fenetre
  (stubs GLFW/GL versionnes) et 58 verifications : lecteurs/exporteurs, soudure,
  anti-superposition, cadrage, plus les familles push/pull (tirage, enfoncement,
  butee + feuille, pull de la feuille, face a coins separes = jupe, echelle par
  objet). Compilation et execution :

```
cmake --build build --target sketch3d_tests
build\Debug\sketch3d_tests.exe
```

## Lot N4.1 (correctifs retour de test : push jusqu'a la feuille)

- **Lot N4.1** : crash (runtime error) au push jusqu'a la butee — le rappel
  d'application a chaque frame lisait/ecrivait hors bornes quand l'objet plie
  etait recapture (le patch etait re-indexe sur le maillage reduit). Le pli
  rentrant indexe desormais le masque sur la copie capturee (dd.tri), pas sur
  le maillage courant ;
- **orientation de la feuille** : les deux windings de la couche coïncident
  a epaisseur nulle, le rayon en capture un au hasard (presque toujours la face
  opposee a la vue). La normale capturee est desormais orientee du cote
  camera : le pull redeplie la matiere du cote visible (comme SketchUp) ;
- **push depuis la feuille bloque au plan** (retour de test : le push
  developpait un volume inverse de l'autre cote) : quand la coque capturee est
  deja reduite a une feuille, il n'y a plus rien a enfoncer — le push cale la
  feuille au plan et seul le pull redeplie la matiere, comme SketchUp, qui
  cale le push-pull a la surface opposee ;
- **composante connexe (N4.1bis cause du retour « la feuille ne se deplie
  toujours que du cote oppose »)** : la capture d'une feuille admet toujours
  les sommets situes au-dela du plan (pour la butee et la detection) ; sur un
  mesh a plusieurs coques disjointes (fichier SketchUp : cube + silhouette dans
  le meme OBJ), la silhouette vivait dans le meme objet : elle bloquait la
  butee, empechait la detection de feuille et violait la reconstruction. Desormais
  la butee, le pli rentrant et la detection de feuille ne touchent que la
  COMPOSANTE du patch — aretes reconnues par position (quantification 1e-6
  relatif, donc les STL a coins dupliques restent rattaches a leur solide) ; un
  sommet partage avec la coque voisine est clone au pli (le tri voisin ne se
  deforme plus) ; la reconstruction feuille preserve verbatim les tris hors
  composante ;
- **sortie du mode Push/Pull (N4.2)** : pendant une extrusion active, la
  touche **P** annule la face en cours ET quitte le mode (retour mode
  selection) — avant, P n'etait actif qu'hors extrusion et il fallait deux
  Echap risque de confusion ; Echap garde son role (annule la face, reste en
  mode), P sort a tout moment ;
- la grille souris -> d (plan de souris pn face a l'ecran, construite dans
  onMouseButton) est verifiee sans NaN et monotone sur toute la fenetre, y
  compris sur une feuille vue de face (fallback pn automatique) ;
- la batterie monte a **75 verifications** (familles composite et feuille bloquee).

## Lot N3

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

- N5 : interface (Dear ImGui) : barre de menus en haut (Fichier/Affichage/Outils
  façon Windows), barres d'outils flottantes dockables (branch docking),
  multi-viewport (deuxieme ecran), cotes, unites, menu Fichier complet
  (ouvrir/enregistrer un projet, importer, quitter)
- ensuite : pipeline par shaders (PBR) pour le rendu photorealiste
- a planifier : fichier de setup utilisateur (regles d import comme la
  normalisation d'echelle, unites par defaut, chemins recents...) ; edition
  libre de maillage (deformations a volonte)

## Notes techniques du lot N1

- **GLFW** pour la fenetre + contexte OpenGL 2.1 (compat).
- **OpenGL fixed-function 1.1** pour le rendu (grille, cube, lumiere fixe) :
  zero chargement d extensions, convenu comme point de depart ; le passage aux
  shaders fera l objet d un lot dedie.
- **Pas de GLU** : matrices perspective (colonne-major) et lookAt calculees a la
  main en doubles.
- Camera orbitale : `dist`, `theta` (azimut), `phi` (elevation bornee 0.02..3.12),
  position en doubles, conversion float seulement au moment de l appel OpenGL.
