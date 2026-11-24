# Sketch3D natif — transcription C++ de la version web

Transcription du prototype web (`index.html` a la racine du depot) en programme
natif Windows, par petits lots testables. Modele geometrique en **double
precision** natif (la version web stockait en Float32).

## Outillages T1.5 (retours de test extrusion) : ancre du drag + zone de saisie

**Version affichee** : la console et la barre de titre indiquent la build
(macro `BUILD_VERSION` en tete de `src/main.cpp`, ex. `T1.5`) — incrementee a
chaque lot ou correctif, pour verifier que l'exe correspond bien au dernier
`git pull`.

- **correctif grave "la face passe en feuille des la capture"** : le plan de
  souris du Push/Pull (plan face a l'ecran passant par la face) est percé par
  le rayon ailleurs qu'au point cliqué (il est ancré sur un sommet du
  triangle, comme dans la version web) ; le `d` brut mesuré des la premiere
  frame valait plusieurs unites sur une grande face et se retrouvait clampé
  d'emblée a la butee : la face s'aplatissait **avant** de suivre le curseur.
  Le deplacement est desormais **relatif au rayon de capture** (`dRef`) : d = 0
  a la capture, la face suit ensuite le curseur (souris vers le haut = tirage) ;
- **pave numerique accepte pendant l'extrusion** : chiffres du pave
  numerique, separateur decimal du pave et **Entree du pave** fonctionnent
  comme la rangee du haut (idem pendant la rotation ; le pavé marchait deja
  pour la duplication) ;
- **zone de saisie unique et bien marquee** : en fin de bandeau, une boite
  blanche adaptee au texte tape (libelle `nombre :`, `dimension :` ou
  `angle :`, curseur clignotant) affiche la valeur en cours — la meme pour les
  trois cas qui acceptent une valeur clavier (duplication, extrusion,
  rotation) ; le texte d'invite est rogné si la fenetre est etroite ;
- **correctif "la valeur tapee n'a aucun effet visible"** : chiffres + Entree
  pendant une extrusion posait bien la valeur, mais la relecture du curseur
  (chaque frame) l'ecrasait des la frame suivante - la face retombait au
  niveau du curseur et l'effet ne reparaissait qu'a la capture suivante
  (impression de "sans effet", et effet devu "affecte a la selection
  suivante"). **Nouveau contrat (retour de test)** : saisir une valeur puis
  **Entree applique la dimension, pose la face et quitte le mode P** - une
  action complete au clavier, sans retoucher la souris (le micro-mouvement
  de main lors d'un clic ne peut plus revenir derriere la valeur) ; le drag
  souris garde tout le reste du comportement (ancre dRef, la face suit le
  curseur) ;
- **signe de la saisie** (retour de test) : le tiret haut ou la touche du pave
  numerique insere un **-** devant la valeur tapee ; negatif = **enfoncement**,
  positif = tirage, la convention du drag live (0 refuse : rien ne se pose) ;
- **butees respectees par la saisie** (demande de test) : une valeur au-dela
  de la matiere se **cale a la butee** au lieu de traverser et ressortir de
  l'autre cote (message `Butee atteinte : valeur calee a ...`) ; lorsqu'un
  push consomme tout l enfoncement, la face devient feuille (le pull
  recreera le volume) ;
- **validation = pose, partout (retour de test)** : meme contrat pour la
  rotation que pour l'extrusion - chiffres + **Entree appliquent l'angle,
  posent la rotation et quittent le mode** (avant, la rotation restait
  active et le moindre mouvement de souris reprenait), la duplication garde
  Entree vide = 1 ;
- **champ nombre de la duplication (retour de test T1.5.5)** : le tiret haut
  ou la touche du pave est desormais accepte dans la boite de saisie ;
  **valeur negative = retrait d'instances** (demande explicite de
  l'utilisateur : corriger une sur-duplication type "55 tape au lieu de 5"
  sans refaire le geste) - les k dernieres copies numerotees de la serie de
  l'objet source sont supprimees, l'**original toujours epargne** ; au-dela
  du disponible, on retire tout ; aucune copie numerotee = message, rien de
  supprime ; hors plage -999..999 = refusee proprement (aucune action) ;
- **saisie d'angle figee pendant la frappe (retour de test T1.5.6)** : des le
  premier chiffre tape pendant une rotation, la piece **ne suit plus la
  souris** (l'angle accumule ne bouge plus) ; Entree pose l'angle et quitte
  le mode - le micro-mouvement de main lors de la frappe ne peut plus faire
  tourner la piece ; la question de l'axe est raccourcie en console (l'invite
  complete vit dans le bandeau de la fenetre) ;
- **separateur du champ nombre (retours de test T1.5.6 a T1.5.8)** : touche
  ignoree (le champ est un entier par conception) mais avec un **avis clair
  dans le bandeau de la fenetre** (flash de 3 s, retour de test : les avis de
  saisie ne doivent pas passer uniquement par la console) - le retrait
  negatif sert justement a corriger une frappe ratee (2.5 malvenu devenant
  25) ;
- **separateur decimal independant de la disposition du clavier (retour de
  test T1.5.7/1.5.8)** : la virgule (separateur francais), le point, la
  decimale du pave et la touche ';' (AZERTY : le point s'obtient via
  Maj + ';') sont acceptes dans la saisie d'angle et de dimension - la
  virgule se perdait dans la T1.5.7 (25,4 tape devenait 254 sur l'angle) car
  le code de touche OEM varie selon la disposition ; la detection passe
  desormais par le NOM de la touche (function `isSeparatorKey` : codes
  connus + `glfwGetKeyName`), la boite affichant un point pour atof ;
- **separateur decimal** : pris en compte pour l'extrusion et la rotation
  (point, virgule ou touche decimale du pave, affiche en point dans la boite ;
  le champ nombre est un entier, sans separateur par conception) ;
- la batterie est a **115 verifications** (+ separateurs : la virgule francaise, le point, la decimale du pave et ';' acceptes ; lettre, tiret, retour arriere, Entree et Echap refutes) (+ enfoncement -0.5 le long de la
  normale ; valeur au-dela de la butee = rien ne depasse le plan ; duplication
  refusee hors plage ; retrait d'instances : -k supprime les k derniers
  numeros de la serie, original epargne, depassement = tout le disponible,
  aucune copie numerotee = retour 0).

## Outillages T1.6 : export cible + suppression + compteur de selection

- **export cible** : n'exporte que l'objet selectionne (7 verifications de
  relecture dans le harnais) ; **T1.6.3 (retour de test)** : tout l'
  **import/export reste au menu Fichier** (plus homogene pour l'utilisateur) ;
  sans selection, un avis s'affiche dans le bandeau (flash 3 s) au lieu d'un
  message console ; les entrees export du menu contextuel sont retirees. ;
  **T1.6.4 (retour de test)** : paquets menu Fichier reorganises - paquet
  **objets** = `Importer un objet...` + `Exporter la selection` (sous-menu),
  paquet **scene** = `Exporter la scene` (sous-menu, OBJ/STL au choix dans
  chaque sous-menu) - pas une ligne par format, le menu ne s'allonge plus
  quand on ajoute des formats ; un format par defaut parametrable reste au
  backlog ;
- **outil Supprimer** : menu contextuel > **Supprimer** - retire l'objet
  selectionne (original ou copie), la selection suit l'objet suivant a la
  meme place, index invalides refuses, liste videe = selection a -1 ;
- **compteur de selection en temps reel** : le **titre de la fenetre**
  affiche constamment le nombre d'objets et l'objet selectionne
  (`Sketch3D - build T1.6 - 3 objet(s) - selection : P2 (2/3) ; selection :
  aucune` sinon) - maj seulement au changement ;
- **surlignage marque de la selection (retour de test T1.6.1)** : les aretes
  de l'objet selectionne passent en **jaune vif epais** (lisible sur tout
  fond et sur piece claire) ; les faces sont reculees d'un poil
  (`glPolygonOffset`) pour que les aretes posees dessus ne se noient plus
  dans le volume - on voit toujours exactement quelle piece est selectionnee
  avant une action (suppression, duplication, export) ;
- **touche Suppr (retour de test T1.6.2)** : selectionner a la souris puis
  supprimer au clavier (poste de travail classique des operations
  sequentielles ; copier/coller au clavier restent au backlog) - la touche
  agit en mode neutre uniquement (jamais pendant une extrusion/rotation ou
  un deplacement) ;
- **raccourci Ctrl+S (retour de test T1.6.4)** : enregistre le projet au
  complet via le meme dialogue que le menu Fichier (`Enregistrer le
  projet...`) ; agit uniquement en mode neutre - jamais pendant les saisies
  dup/extrusion/rotation actives, comme tous les raccourcis du bloc neutre ;
- **echelle Echap (retour de test T1.6.5)** : une seule Echap = **sortir de
  l'etat en cours** - pendant une frappe extrusion/rotation, elle annule
  l'action ET quitte le mode (meme logique que le clic droit) ; en mode P/R
  sans action en cours, elle quitte le mode ; avec une selection, elle la
  deselectionne (`selection annulee`) ; **T1.6.6 (retour de test)** : Echap ne
  ferme JAMAIS l'application (un rebond de clavier ne doit pas tout fermer) -
  quand il n'y a rien a annuler, Echap est tout simplement silencieux et la
  fermeture reste volontaire par Fichier > Quitter ;
- la batterie est a **122 verifications** (+ export cible OBJ/STL verifie
  par relecture : un seul objet exporte et le bon, STL binaire taille
  exacte 84+50x12 ; suppression : selection suivie, index invalides,
  liste videe, selection a -1).

## Multi-selection T1.7 : Ctrl+clic (actions multi)

- **Ctrl+clic = ajoute/retire** un objet de la selection (n'importe quel objet,
  n'importe quel ordre) ; **Ctrl+clic sur le dernier selectionne = deselection
  totale** (facon SketchUp) ; **clic gauche simple = selection seule** (remplace
  la multi) ; Ctrl+clic dans le vide : rien (la selection est protegee) ;
- une seule selection surlignee (aretes jaune vif) pour TOUS les objets :
  on voit exactement ce que l'action concernera avant de la lancer ;
- **Echap vide la selection complete** (puis quitte un mode en attente s'il
  y en a un - meme echelle que T1.6.6) ;
- **le titre affiche le total** : `selection : 3/12 objets` en multi,
  `selection : Nom (k/N)` a un seul objet, `aucune` sinon ;
- **Suppr = supprime toute la selection** (1 objet comme plusieurs, menu
  contextuel egalement) : message console avec le detail, selection videe ;
- **Dupliquer... s'applique a chaque objet de la selection** : le nombre tape
  produit k copies par objet (noms numerotes `Src (k)`), et une valeur
  negative retire k copies de CHAQUE serie de la selection (les indices
  sources sont re-resolus par nom a chaque coup, jamais stale) ;
- **Export de la selection (menu Fichier, sous-menus OBJ/STL) = multi** :
  tous les objets selectionnes partent dans le meme fichier (ordre des
  indices, `o Nom` par objet), des 2 objets comme des 12 ;
- la batterie passe a **133 verifications** (+11 : toggle multi, ordre
  d'export, STL 24 tris taille exacte, duplication/retrait multi, suppression
  multi avec reste intact et selection videe).

### Point 4 du test T1.7 (crash Ctrl+clic) - binaire T1.7.1 (rebuild T1.7.2 : sequence seule)

- **cause racine** : sur un Ctrl+clic PUR (aucune selection prealable, donc
  selection portee par la multi, principal reste a -1), l'ancien titre lisait
  `g_objs[(size_t)g_selected]` brut = acces a `g_objs[-1]` = assertion MSVC
  "vector subscript out of range" (reproduite et prouvee hors Windows) ;
- la build T1.7.1 lit le titre par la liste elagee et durcit aussi : le drag
  (selection principale perimee = retour neutre), la promotion multi (indice
  perime elague, premier valide promu ou deselection propre), et les tris
  dont les indices depassent les donnees (rendu, rayons, capture) sont
  ignores (maillages degeneres) ;
- la batterie passe a **137 verifications** (+4 : titre selection pure Ctrl,
  promotion avec elagage, promotion tout-perime, drag perime) ;
- **sequence de test corrigee** : plus de `git pull` du tout sur les branches
  de test - le pull a echoue chez l'utilisateur (`Committer identity unknown`
  : aucune identite git configuree, git refuse le merge ; resultat : branche
  locale divergee, build compile encore l'ancien code, titre freeze a T1.7) ;
  on remplace par **fetch + reset --hard** : pas de merge, pas d'identite
  requise, la branche de test est un simple miroir du depot distant donc le
  reset est sans risque - le titre de la fenetre fait foi quand a la version ;
  (commit sequence seule : BUILD_VERSION passee a T1.7.2, titre attendu **T1.7.2**)

```
cd C:\Users\rico\source\repos
git switch <branche>
git fetch origin
git reset --hard origin/<branche>
cd desktop
cmake --build build
build\Debug\sketch3d.exe
```
## Point de vue T1.8 : deplacer la vue (pan) + recentrage (C)

- **clic molette + glisser = pan de la vue** : la cible camera se deplace
  dans le plan de l'ecran ; l'echelle suit la distance (l'objet sous le
  curseur y reste pendant tout le pan) ; alternative **Shift + clic gauche
  dans le vide + glisser** (souris sans molette) ; relacher = fin du pan ;
- **C = centrer la vue sur le point vise** : le curseur vise un objet ->
  la camera recentre sur le point d'impact ; sinon le sol (y = 0) si le
  rayon le touche ; theta/phi/dist conserves (la vue tourne, ne zoome pas) ;
  agir sur un objet lointain : pointez-le, C, puis F pour cadrer ;
- la Camera reste une structure copiable et les helpers (`cameraPan`,
  `pickPointFromRay`) sont purs sur `Camera&` : les futures vues multiples
  (fenetres 2, trajectoires) reutiliseront les memes fonctions ;
- batterie : **144 verifications** (+7 : pan vertical et horizontal (signe
  et echelle exactes), hauteur de fenetre nulle ignoree, impact objet,
  impact sol, rayon vers le ciel = aucun point ; sur la base durcie T1.7.1).

## Rotation multi T1.9 (cas 1 : chaque objet sur son propre centre, angle commun)

- **en selection multiple (Ctrl+clic), R + lettre d'axe (X/Y/Z)** tourne les
  objets selectionnes **individuellement** : chacun autour de son PROPRE
  centre (comme le mono), avec le **meme angle et le meme axe** pour tous ;
- les objets restent a leur place (pas de translation de groupe) - futur cas
  2 (pivot = barycentre du groupe) et cas 3 (pivot choisi par clic) utilisent
  le meme moteur : la capture stocke deja une base + un pivot PAR objet ;
- mêmes revues que la rotation mono : souris en direct (0.25 deg/pixel),
  chiffres + Entree = angle exact pose et quitte le mode, clic gauche pose,
  clic droit ou Echap annule TOUT (chaque objet retrouve sa base capturee) ;
- bandeau : `Rotation multi Z : +12.0 deg sur 3 objets (chacun sur son propre centre)` ;
- en mono (un seul objet selectionne) : chemin historique strictement inchange ;
- `applyRotationAt` gere desormais le pivot en parametre explicite (les 3 cas
  de pivot multi n'ont plus qu'a fournir un pivot different) ;
- **correctif T1.9.1 (fenetre entierement grisee pendant la rotation multi)** :
  bug ancien de `drawTextClipped` mouille par le message de bandeau multi
  (plus long) - l'abreviation repartait de la taille gonflee par les "..."
  (substr sur `txt.size() - 1` puis `+= "..."`) : aucune iteration ne pouvait
  repasser sous la largeur une fois le message trop long, boucle infinie =
  fenetre sans pompage = grisee "ne repond pas", bandeau fige. Correction :
  la longueur de reference diminue reellement a chaque tour + garde-fou final ;
  message bandeau multi raccourci ;
- batterie : **159 verifications, 0 echec** (+12 : capture d'une base par
  objet, pivot propre, angle commun a la souris, preview exacte au vertex,
  voie numerique 90 + Entree, portees permutees par objet, position figee,
  volumes preserves, annulation complete, regression mono).

## Rotation multi T1.10 (cas 2 : pivot = barycentre du groupe)

- en selection multiple, la rotation multi (R + axe) dispose d'un **pivot
  configurable a chaud** : **B** bascule entre `centre propre` (cas 1 T1.9,
  defaut : chaque objet tourne sur lui-meme) et **barycentre du groupe**
  (cas 2 : le lot pivote rigidement autour du centre de la boite englobant
  toutes les geometries capturees - GA et GB s'echangent par +180 deg) ;
- l'**angle courant est conserve** a la bascule : l'application part toujours
  de la base capturee, le pivot du groupe est fige a la capture (calcule sur
  les bases donc stable pendant la rotation) ; rejouer B = retour au centre
  propre, tout a l'identique ;
- pose (clic gauche ou chiffres + Entree) et annulation (clic droit/Echap)
  inchanges (annulation = retour a l'etat de la capture ; mono strictement
  non concerne par B sans selection multi) ;
- bandeau borne : `Rot multi Z 3 objets (barycentre du groupe) : +25.0 deg ...` ;
- le module pivot complet (sommet/face accroche, coordonnees, point clique,
  centre du monde...) reste planifie en phase B (B8, etude de conception) ;
- batterie : **171 verifications, 0 echec** (+12 : pivot groupe exact, bascule
  B a chaud avec angle conserve, echange des positions par +180, ancre pos
  inchangee, preview exacte autour du barycentre, annulation, mono/B isole).

## Outillages T1 : duplication + rotation

- **bandeau de statut dans la fenêtre** (bas de l'écran, façon barre de
  statut) : affiche en permanence l'invite du mode courant et la valeur en
  cours (nombre d'instances tape, extrusion en cours, rotation en direct...)
  — les questions/réponses ne passent plus par la console ;
- **regle generale des transformations** : le **clic droit** revient au neutre
  partout — annule la transformation en cours (extrusion posee en suspens,
  rotation en cours) ET quitte le mode (le deplacement en vol ne peut pas etre
  annule ainsi : le clic droit exige de relacher le bouton gauche, l'objet est
  deja pose) ;
- **menu contextuel** : il n'apparait que si un objet est deja **selectionne**
  (clic gauche) ; clic droit dans le vide ou sans selection : rien (juste un
  rappel en console) ;

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
  **clic droit ou Echap** pendant une rotation : annule la rotation continue ;
  **appuyer une autre lettre (X/Y/Z) pendant une rotation** : pose la rotation
  en cours et repart directement de la geometrie courante sur le nouvel axe,
  sans deselectionner ; **clic droit hors rotation** (ou Echap, ou R) : quitte
  le mode rotation ;
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
