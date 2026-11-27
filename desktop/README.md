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

## Barre d'outils flottante B1 (v1)

- Petite fenetre outil `Outils` (WS_EX_TOOLWINDOW, gadget, depisable) avec
  boutons : **Selection, P (Push/Pull), R (Rotation), F cadrer, H origine,
  Copier, Coller** ; deplacable n'importe ou par sa barre de titre (2 ecrans
  inclus) ; la croix la CACHE (la retrouver au lancement suivant) ;
- **un seul chemin de code** : les boutons et les touches P/R passent par le
  meme dispatcher `runToolCommand` - le bouton ne peut pas diverger du
  raccourci (modes exclusifs, modes P/R mutuellement exclusifs, annulation de
  la transformation en cours comprise) ;
- bouton Selection = chaine Echap (sans jamais fermer l'application) :
  1er clic = annule l'etat en cours (face en suspens / rotation en cours) et
  quitte le mode, 2e clic = deselection ;
- Copier / Coller = Ctrl+C / Ctrl+V (T1.12, mono et multi) ;
- batterie : **211 verifications, 0 echec** (+13 : dispatcher boutons vs
  raccourcis, exclusivite P/R, annulation via bouton, copier/coller boutons,
  cadrer/origine boutons).

## Barre d'outils B1.1 : menu Affichage, verticale, aimantation

- **menu Affichage** (a cote de Fichier) : `Barre d'outils` (coche = visible)
  et `Barre verticale` (coche = verticale) ; **rappel facile** : la croix
  cache la barre ET decoche la ligne du menu (et grave la preference),
  le menu la fait revenir ;
- **orientation (B1.5 : radios)** : `Barre horizontale` / `Barre verticale`
  en PAIRE RADIO (coche = etat reel, plus de libelle ambigu) ; bascule a
  chaud ; horizontale = 2 rangees de 4, verticale = 1 colonne ;
- **B1.3 : la barre suit la fenetre principale** - sa position est un
  decalage par rapport au coin de la fenetre (plus par rapport a l'ecran) :
  bouger / maximiser / redimensionner la fenetre emporte la barre,
- **aimantation** : en fin de deplacement de la barre,'elle se colle au bord
  de la FENETRE PRINCIPALE si elle passe a 24 px ou moins (gauche, droite,
  haut, bas) ;
- **persistance** : visibilite, orientation et position gravees dans la
  section `[outils]` du `sketch3d.ini` (visible, vertical, x, y) a chaque
  changement - la barre revient exactement OU elle etait au lancement ;
  valeurs non numeriques tolerees (parse = atoi, fichier toujours lisible) ;
- batterie : **217 verifications, 0 echec** (+4 : section [outils] absente =
  defauts, ini edite a la main relu, round-trip fermeture -> lancement,
  valeurs non numeriques tolerees).

## Menu Preferences B1.4 (choix au vol)

- **menu `Preferences`** (a cote de Fichier, apres Affichage) :
  `Export de la scene` -> **OBJ / STL** (coche sur le choix courant) ;
  `Export de la selection` -> **OBJ / STL** ;
- **un seul chemin de code** : les radios passent par `noteExportFormat`, le
  meme mecanisme que les exports reels - memoire `[exports]` gravee
  immediatement dans `sketch3d.ini`, impossible de diverger ;
- **B1.6 : coches toujours justes** - `noteExportFormat` est le GOULOT
  UNIQUE des formats : tout export (menu Fichier, raccourcis O/S) comme tout
  choix Preferences y passe, et les coches du menu Preferences sont
  resynchronisees a chaque fois (plus d'incoherence constatee en test) ;
- **B1.7 : UN SEUL lieu de choix du format** (demande utilisateur, evite les
  collisions) - les sous-menus OBJ/STL du menu Fichier sont SUPPRIMES :
  `Fichier > Exporter la scene...` / `Exporter la selection...` exportent au
  FORMAT PREFERE (dialogue pre-filtre, format reel = extension du fichier
  choisi, grafe via noteExportFormat) ; les raccourcis O/S historiques sont
  supprimes aussi (collisions) ; l'import garde sa touche I ;
- le menu s'etoilera au fil des B (unites, preferences d'outils...), le
  mecanisme B0 est pret a chaque fois.

## Homogeneite des commandes B1.5 (regle systematique)

- tous les identifiants de COMMANDE (outils, Affichage, Preferences) sont
  derniers et contigus dans l'enum (`kIdCommandStart`) ;
- tout producteur (menu Fichier, clic droit, boutons de la barre, clavier)
  emet ses commandes via le dispatcher `runToolCommand` exclusivement ;
- les FUTURS producteurs - mode persistance, mode macro - emettront leurs
  commandes au meme endroit (persistance = liste de commandes rejouee,
  macro = script traduit en commandes) : l'homogeneite est un prerequis,
  pas un style.

## Fichier de setup B0 (sketch3d.ini)

- **INI editable a la main** pose a cote de l'exe (`sketch3d.ini`, sans
  registry) ; charge au lancement ;
- contenu v1 : **geometrie de fenetre** (x, y, largeur, hauteur, maximise =
  restauration au lancement, position 2 ecrans incluse, sauvegarde a la
  FERMETURE), **dernier projet** ouvert/enregistre, **formats d'export
  reels** (scene / selection -> obj ou stl, memorises au fil des exports) ;
- **sauvegarde a chaque changement de preference** (choix valide : dernier
  projet note a l'ouverture/enregistrement reussis, format note a chaque
  export reel) ;
- parse tolerant : commentaires (`#`, `;`), cles ou valeurs inconnues
  ignorees, w/h <= 0 ignores (defauts du programme), fichier absente =
  premiere utilisation sans bruit ;
- batterie : **198 verifications, 0 echec** (+7 : fichier absent, parse
  tolerant, dernier projet, format invalide ignore, sauvegarde immediate,
  round-trip fermeture -> relancement, INI lisible a la main).

## Copier-coller T1.12 (Ctrl+C / Ctrl+V)

- **Ctrl+C** : copie la selection courante (mono OU multi, Ctrl+clic) dans un
  presse-papiers interne (copies entieres : geometrie, couleur, echelle,
  position) ; sans selection : rien + message bandeau ;
- **Ctrl+V** : colle le groupe copie decale vers +X d un pas (largeur X de la
  boite du groupe copie + petit espace) ; l espacement interne du groupe reste
  identique ; chaque Ctrl+V successif decale encore d un pas (serie visible,
  facon duplication) ; **le groupe colle devient la selection** ;
- nommage type duplication : `Nom (1)`, `Nom (2)`... ;
- le presse-papiers survit aux suppressions (le collage repart des copies) ;
- `C` sans Ctrl reste le recentrage sur le point vise ; **Ctrl+X (couper) :
  plus tard** (copie + suppression existent deja separement) ;
- aide du bandeau : `Ctrl+C/Ctrl+V : copier/coller` ;
- batterie : **191 verifications, 0 echec** (+9 : copie mono/multi, position
  decalee exacte, serie visible, selection = groupe colle, presse-papiers
  independant des suppressions, presse-papiers vide = rien pose).

## Deplacement de groupe T1.11 (drag multi)

- **clic gauche simple sur un objet deja membre d'une selection multi**
  (de plus d'un objet) : la selection multi est **conservee** (pas de
  reduction au seul objet clique) et le drag deplace **tout le groupe** ;
- le deplacement suit le modele mono : delta dans le plan camera passant par
  le point de saisie, **verrou d'axe X/Y/Z** (touches tenues) actif pour tout
  le groupe (meme delta projete) ;
- clic sur un objet HORS selection : comportement mono historique inchange
  (selection seule + drag singleton) ; Ctrl+clic reste la selection pure
  (jamais de drag) ; le double-clic ne recentre que hors multi ;
- relachement gauche = pose, capture de groupe videe ; console au demarrage :
  `deplacement multi : N objets` ;
- batterie : **182 verifications, 0 echec** (+11 : membres captures avec leur
  position de depart, delta plan camera identique exact des deux membres,
  verrou X herite commun, pose + selection intacte au relachement, mono hors
  multi inchange).

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

## B2.1 : outil Ligne (fil multi-segments)

Conception B2 validee (ROADMAP.md) avant tout code. L'outil Ligne pose un FIL
de A vers B et au-dela : chaque clic gauche ajoute un sommet (les segments
sont lies, l'objet est unique), **clic droit = fin** (exception assumee a la
regle "clic droit = neutre" : ici il conclut), **Echap** = tout annuler,
**Entree vide** = fin, **chiffres + Entree** = longueur exacte du segment
courant dans la direction du curseur (pose + sortie du mode, meme contrat
que le reste). Pose sur le plan du sol en v1.

- l'objet pose est un fil tri-soup sans triangle : 3 sommets, aretes chaine ;
  `computeObjectDerived` reconstruit la chaine quand un objet n'a aucun
  triangle, donc la topologie survit au save/load, a la rotation, a la
  duplication ;
- le fil n'a pas de volume : pas de push/pull dessus (decide) ; il n'apparait
  pas encore dans les exports OBJ/STL (ouvert pour B3 : chemins) ;
- menu **Outils** cree (Selection/Echap, Extraire une face/P, Tourner/R,
  Ligne/L, Cadrer/F, Origine/H, Copier/Ctrl+C, Coller/Ctrl+V) : les
  raccourcis sont rappeles dans le menu qui les propose (regle B1.7c) ;
- ordre des menus = Fichier, Preferences, Outils, Affichage (Preferences
  reste a cote de Fichier, B1.4) ;
- bouton "Ligne" ajoute a la barre flottante (8 boutons) ; apercu jaune du
  fil vers le curseur pendant la saisie ;
- moteur de tests : bloc 32 = 12 nouvelles verifications, total **229,
  0 echec** ; stubs completes (GLFW_KEY_L, GL_LINE_STRIP).

## B2.5 : fermeture de la ligne (fait avant B2.2-B2.4, sur demande)

Un clic sur le point de DEPART alors que le fil compte au moins 3 sommets
(tolerance `filPickTol`, la meme que la capture du fil) ferme la boucle. Le
comportement est choisi par la COMMANDE qui arme l'outil (B2.5b, retour de
test : rien n'est memorise, aucune preference, aucune cle dans le INI) :

- **Outils > Ligne (fermee en surface) - L** : la boucle cree une feuille
  FUSEE au fil en UN objet (fidele SketchUp) - fan triangulaire (0, i, i+1)
  + aretes de bord ; la feuille est etirable au push/pull ;
- **Outils > Ligne (fil ouvert) - Maj+L** : la boucle reste un fil - arete
  de fermeture n-1 -> 0 ajoutee apres la derive de la chaine, aucun triangle.

B2.5b corrige aussi un bug de relocalisation : la boucle fermee etait dessinee
decalee du premier point (coordonnees monde stockees comme locales, le rendu
ajoutant encore o.pos).
- note v1 : `computeObjectDerived` derive les aretes de TOUS les triangles
  du fan -> un carre ferme montre 5 aretes (4 bords + 1 diagonale). La
  difference bord/diagonale sera naturelle avec le VRAI SOLIDE de B6 ;
- moteur de tests : +7 verifications (armement L / Maj+L ; feuille = 2 tris /
  5 aretes / volume plat nul / 4 sommets sans doublon ; fil ouvert = 4
  aretes, 0 tri ; sortie du mode ; noms uniques), total **240, 0 echec**.

## B2.2 : outil Rectangle

Maj+R (bouton « Rectangle » de la barre, entree du menu Outils). Clic 1 =
coin, souris = apercu du contour au sol, clic 2 = coin oppose pose. v1
aligne aux axes (largeur en X, profondeur en Z), pose au sol y = 0.

- objet pose = feuille pleine de 2 tris (normale +y) + aretes de bord, UN
  objet, etirable au push/pull (coherent avec la ligne fermee B2.5) ;
- chiffres + Entree = dimensions exactes « L;H » (largeur, profondeur ;
  separateur point-virgule : la virgule est proscrite, confusion decimale
  probable sur clavier FR) ;
  le quadrant du curseur par rapport au premier coin oriente le rectangle
  (fidele SketchUp : la diagonale dirige), puis pose + sortie du mode ;
- clic droit ou Echap = annulation SANS objet pose (mecanique commune B2) ;
- nommage uniqueName commun aux profils B2 (« Rectangle », « Rectangle (2) ») ;
- stub corrige au passage : GL_LINE_LOOP valait 0x0003 (confusion avec
  GL_LINE_STRIP), remis a la vraie valeur 0x0002 ;
- B2.2c (retour de test, 2 bugs) :
  - push/pull mort sur le rectangle trace a la souris : la garde d'entree de
    makeExtrude (tri.size() < 9) refusait toute feuille de 2 tris ; relachee
    a 1 tri complet - une feuille est une face valide, tout le reste (flip
    camera, butees, recreation du volume) gerait deja les feuilles ;
  - clavier FR : sur AZERTY le ';' est NON majuscule (rangee basse
    , ; : !), donc Maj n'intervient pas ; la saisie utilise l'ETIQUETTE
    REELLE de la touche (glfwGetKeyName) : ce qui est tape s'affiche (';',
    ',' decimale, '.'), quelle que soit la disposition ; repli par code de
    touche si le nom manque (SEMICOLON = ';', PERIOD/KP_DECIMAL = '.',
    COMMA = ',' decimale) ; le parseur convertit ';' en separateur et ','
    en '.' decimale ;
  - convention generale : « ; » = separateur de champs, « , » et « . » =
    decimales ;
- REGLE GENERALISEE (B2.4f, retour de test Maj+A) : GLFW rapporte les
  touches par POSITION QWERTY (codes US) - sur AZERTY la touche etiquetee
  A arrive en GLFW_KEY_Q, le M en GLFW_KEY_SEMICOLON, etc. Toute
  comparaison de touche LETTRE passe donc par keyIsLetter (etiquette
  reelle via glfwGetKeyName, repli par code) : JAMAIS de comparaison
  directe du code GLFW pour une lettre. Les touches de controle
  (Entree, Echap, Suppr, Backspace, moins, KP_*) restent comparees par
  code : positions universelles ;
- REGLE DES VARIANTES (B2.5b, generalisee B2.7b - validation
  utilisateur : « c'est la bonne facon de faire ») : tout choix de
  conception a deux usages se traduit par des VARIANTES COMMUTABLES par
  la COMMANDE d'armement (deux entrees de menu / deux raccourcis), RIEN
  de memorise entre les traces (defaut a chaque lancement) ; la bascule
  reste disponible A CHAUD pendant un trace quand les points survivent
  au changement (ex. spline passage/controle, Maj+S/Maj+B) ;
- moteur de tests : +7 verifications (B2.2c/d).

## B2.3 : outil Cercle

Maj+C (bouton « Cercle » de la barre, entree du menu Outils ; C seul =
recentrage sur le point vise, Ctrl+C = copie : Maj+C rend la main AVANT le
recentrage). Clic 1 = centre (au sol), souris = apercu (cercle jaune au
rayon courant + segment centre-curseur), clic 2 = rayon pose. Echap/clic
droit = annulation sans objet (mecanique commune B2).

- objet pose = feuille fan de 24 tris (centre + boucle de 24 sommets =
  25 sommets), normale +y (boucle en (cos, -sin)), UN objet, etirable au
  push/pull ; 24 segments fixes en v1 ;
- aretes derivees = 24 bords + 24 rayons du fan (comme la diagonale du
  rectangle, se rattrapera avec le vrai solide de B6) ;
- chiffres + Entree = rayon exact : UN seul nombre (pas de direction a
  prendre, le cercle est invariant par rotation) ; decimales FR acceptees
  (',' ; et '.' = decimale, convention B2.2b/B2.2d) ;
- nommage uniqueName commun (« Cercle », « Cercle (2) ») ;
- B2.3b (retour de test) : le pavé numerique n'affichait que des decimales
  - la simplification de la saisie avait ecrase le mapping chiffres ;
  retabli (chiffres = chiffres, separateur = decimale) ;
- moteur de tests : +9 verifications, total execute **267, 0 echec**.
  Nouveau : le CHECK compte ses executions (g_tot) et le resume affiche
  le total execute - les totaux anterieurs comptaient des lignes de
  sortie, sous-estimes (messages multi-lignes) ;

## B2.4 : outil Arc (Arc 2 points de SketchUp)

Maj+A (bouton « Arc » de la barre, entree du menu Outils). 3 clics :
depart (P1), fin (P2 = la corde), bombement (clic 3 = pose, l'arc flechit
a la souris). Echap/clic droit = annulation sans objet (mecanique B2).

- les 3 points (P1, B, P2) definissent LE cercle unique (mediatrices) ;
  les 13 points poses sont recalcules EXACTS sur le cercle ; le bombement
  clic ne sert qu'a definir ; B2.4d : limite des 180 deg LEVEE - toute
  fleche > 0 est valide, au-dela de la moitie de la corde l'arc est
  MAJEUR (> 180 deg, fleche 5-10x la corde = proche du cercle complet) ;
- objet pose = FIL OUVERT (choix utilisateur) : 12 segments (defaut
  SketchUp arc), sans triangle, chaine derivee comme la ligne (capture
  filRayHit, hors exports OBJ/STL) ; completable apres coup par une ligne
  de 2 points pour fermer une surface etirable (remarque utilisateur) ;
- saisie exacte en deux temps : apres le depart, chiffres + Entree =
  longueur de corde (direction = curseur) pose la fin ; apres la fin,
  chiffres + Entree = fleche exacte (cote = curseur) pose l'arc ;
  decimales FR (convention B2.2b/B2.2d) ;
- nommage « Arc », « Arc (2) »... ; 11e bouton de la barre ;
- moteur de tests : +9 verifications, total execute **276, 0 echec** ;
- B2.4b (retour de test, grave) : le tableau des boutons de la barre
  etait reste a 9 alors que la creation en compte 11 (Cercle, Arc) -
  DEBORDEMENT de 2 pointeurs au demarrage = corruption memoire (boucle
  folle constatee par l'utilisateur) ; g_toolBtn[11] + toolLayout derive
  maintenant sa taille du tableau (synchro automatique, plus de
  desynchronisation possible) ; lecon : les sections Windows ne sont
  JAMAIS compilees en sandbox, seul le build MSVC les voit ;
- B2.4e (retour de test : Maj+A mort alors que Maj+R/Maj+C fonctionnent) :
  GLFW rapporte les touches par POSITION QWERTY (codes US) - sur AZERTY la
  touche etiquetee A arrive en GLFW_KEY_Q (R, C, L, P... aux memes
  positions, d'ou les autres raccourcis fonctionnels). Correctif :
  comparateur keyIsLetter par ETIQUETTE reelle (glfwGetKeyName, regle
  B2.2d de la saisie, repli par code) applique a TOUS les raccourcis
  lettres (armements, Ctrl+C/V/S, F/H/I, recentrage C) ;
- B2.4d (retour de test : "des formes ressemblent a un arc mais dont la
  fleche fait 5 ou 10 fois la corde") : limite des 180 deg levee - la
  construction par cercle unique (3 points) gere deja les grands arcs ;
  gardes remplacees : fleche > 0 uniquement (arc majeur > 180 deg possible,
  proche du cercle complet pour les tres grandes fleches) ; +2
  verifications executees = **278, 0 echec** ;
- B2.4c (retour de test, build B2.4b confirme) : Arc toujours absent de la
  barre + Maj+A sans effet + fleche + Entree sans effet. Trois pieges
  Windows corriges : (1) toolLayout dimensionnait la fenetre EXTERIEURE
  avec la taille CLIENT necessaire - la barre de titre rognait la
  derniere rangee (Rectangle, Cercle, Arc invisibles depuis B2.3) ;
  correctif AdjustWindowRect ; (2) un clic sur un bouton Win32 vole le
  focus clavier a la vue 3D (les touches mouraient jusqu'au clic suivant
  dans la vue) ; SetFocus rendu apres chaque commande menu/bouton ;
  (3) les gardes du dispatcher rejetaient en SILENCE : message desormais
  affiche ("terminez d'abord le mode en cours").

## B2.6 : outil Polygone quelconque

- Maj+P (convention Maj + lettre des formes de base ; P seul = Push/Pull) ;
- N clics = sommets au sol (y = 0), apercu : contour deja clique (ferme,
  GL_LINE_LOOP) + rubber band vers le curseur ;
- fermeture = clic sur le depart, clic droit ou Entree (>= 3 sommets,
  tolerance filPickTol, meme mecanique que la ligne B2.5) ; SURFACE SEULE
  (choix utilisateur - le fil ouvert est deja couvert par Ligne Maj+L) ;
  pas de saisie numerique v1 ;
- pose : feuille fan (0,i,i+1) en UN objet (n-2 triangles), ancre locale =
  premier sommet (pos + v = monde), etirable au push/pull ; aretes derivees
  = bords + diagonales du fan (v1, comme la ligne fermee) ;
- Echap seul = sortie sans rien poser (mecanique B2) ; bandeau = etat
  de la commande seule (regle B1.7c) ; nommage unique « Polygone » ;
- v1 : eventail correct pour polygones CONVEXES (concaves = reprise avec le
  vrai solide B6) ;
- 12e bouton de la barre (g_toolBtn[12], taille toolLayout derivee du
  tableau - lecon B2.4b respectee) ; menu Outils ; entete console ;
- B2.6b (retour de test : « le fil disparait des que l'on sort par clic
  D ») : clic droit ou Entree = VALIDER la surface (comme un clic sur le
  depart, polyFinish), Echap SEUL sort sans poser ; message « au moins
  3 sommets » si validation prematuree ; pave numerique inclus (KP_ENTER) ;
- moteur de tests : +8 verifications (B2.6) puis +2 (B2.6b : validation
  a 3 sommets = posee, refus a 2 sommets = mode conserve), total
  **290, 0 echec**.

## B2.7 : outil Spline (dernier trace de base)

- Maj+S (Ctrl+S = enregistrer, aucune collision ; convention Maj + lettre) ;
- points de PASSAGE (choix utilisateur, avec reserve de tester plus tard
  les points de controle) : la courbe cubique C1 passe par chaque clic ;
- parametrisation centripete (sqrt des cordes, tangentes Barry-Goldman) :
  robuste aux espacements irreguliers (pas de boucle ni de pic) ;
- entree unique : clic = point (doublon ignore) ; clic sur le depart
  (>= 3 points, tolerance filPickTol) = boucle FERMEE lissee ; clic droit
  ou Entree (pave inclus) = VALIDER la courbe ouverte (>= 2 points, regle
  B2.6b) ; Echap = sortir sans rien poser ;
- pose : fil discretise (16 sous-segments par intervalle, v1 fixe) en UN
  objet sans triangle, ancre locale = premier point (pos + v = monde),
  aretes = chaine derivee ; boucle fermee = sommet final = depart (arete
  de cloture naturelle) ; etirable au deplacement (filRayHit) ;
- apercu : courbe lisse a travers les points cliques + droite en rubber
  band vers le curseur (prolongement droit en v1) ; pas de saisie
  numerique v1 ; bandeau = etat de la commande seule (regle B1.7c) ;
- 13e bouton de la barre (g_toolBtn[13], taille toolLayout derivee du
  tableau) ; menu Outils ; entete console ;
- moteur de tests : +9 verifications executees, total **299, 0 echec**.

### B2.7b : variante points de controle (retour de test)
- retour : « j'aurai du prendre tout de suite le choix points de
  controle » → variante COMMUTABLE (regle B2.5b : choix par la commande,
  rien de memorise) :
  - Maj+S = Spline (passage) : la courbe passe par chaque clic ;
  - Maj+B = Spline (points de controle) : les clics tracent le polygone
    de controle, la courbe glisse a l'interieur (facon Rhino) ; noeuds
    serres uniformes (Cox-de Boor) : depart et arrivee EXACTS si
    ouverte, boucle periodique si fermee ; degre reduit (3, 2, 1)
    automatiquement pour les petits nombres de points ;
  - bascule Maj+S/Maj+B A CHAUD pendant un trace, points conserves ;
  - polygone de controle visible (trait sombre) sous la courbe en
    apercu ; bandeau et messages de pose/presence par variante ;
  - 14e bouton « Spline ctrl » + 2e entree menu Outils ;
- moteur de tests : +5 verifications (armement Maj+B, noeuds serres =
  extremites exactes, courbe interieure au carre de controle, boucle
  fermee), total **304, 0 echec**.

### B2.7c/B2.7d : menu groupe + span corrige (retours de test)
- B2.7c : menu Outils groupe par barres horizontales (les variantes
  d'une meme fonction isolees) ;
- B2.7d (retour : « tout ce que j'obtiens ce sont 2 segments de droite »)
  : BUG du span - sur les noeuds SERRES (repetes aux extremites de la
  courbe ouverte), le span se calcule par findSpan (NURBS Book A2.1),
  PAS par floor(u) ; l'ancien code reduisait la courbe a du rectiligne ;
  le ferme (noeuds uniformes) garde floor(u) ; check renforce = valeur
  EXACTE de Bezier au milieu du carre de controle (aurait attrape le
  bug) ; total **305, 0 echec** ;
- MODE D'EMPLOI spline (controle) : chaque clic pose un COIN du
  polygone de controle (trait sombre) ; la courbe jaune glisse a
  l'interieur et ne touche que ses EXTREMITES ; pour serrer la courbe
  vers une zone, ajoutez des points (les doubler au meme endroit la
  plaque contre ce point) ; le polygone n'est PAS pose - seule la courbe
  discretisee devient l'objet fil.

## B2.7e : edition des traces apres coup (E1 : ligne + spline)

Tout objet porte ses PARAMETRES DE CONSTRUCTION (source de verite), la
geometrie n'est qu'un derive regenerable par les MEMES fonctions que le
trace (un seul chemin de code). Generalise plus tard a toutes les formes
(rectangle, cercle, arc, polygone, extrusions - phases E2/E3).

- **E** (ou bouton « Editer ») : entre/sort du mode edition du trace
  selectionne (une ligne ou une spline - sinon message) ;
- poignees : petits carres verts sur les points de definition ; clic =
  saisie (orange), drag au sol = deplacement avec recalcul CONTINU ;
- clic sur la courbe (hors poignee) = ajouter un point de definition a
  l'intervalle le plus proche ; poignee + **Suppr** = supprimer (garde :
  minimum 2 points ouvert, 3 ferme) ;
- **O** = bascule ouvert/ferme a chaud (la ligne fermee retrouve sa
  feuille si elle en avait une) ; **Maj+S/Maj+B** = bascule variante
  spline passage/controle a chaud, points conserves (regle des variantes) ;
- Echap ou clic droit = quitter (mode exclusif : les autres touches sont
  avalees) ;
- persistence : bloc optionnel `trace type ferme surface npts` dans le
  .s3projet (peek par ligne avec retour arriere du flux = vieux projets
  relisibles) ; total **330, 0 echec**.

### B2.7e E2 : formes de base editables (rectangle, cercle, arc, polygone)

Les MEMES poignees, les MEMES regles - chaque forme porte ses parametres
et se regenere par SA construction d'origine :

- **Rectangle** : 2 poignees = les coins ; les coins canoniques (min/max)
  sont recalcules, la feuille reste normale +y ;
- **Cercle** : 2 poignees = centre + point du rayon (toujours SUR le
  cercle, angle 0) ; le rayon suit la distance centre -> poignee ;
- **Arc** : 3 poignees = depart, fin, bombement ; l'arc regenere passe
  EXACTEMENT par les 3 ; si le bombement est amene sur la corde (arc
  degenere), la DERNIERE geometrie valide est conservee ;
- **Polygone** : poignees sur les sommets ; clic sur un bord = sommet
  ajoute, poignee + Suppr = supprime (garde : minimum 3 sommets) ;
  ouvert/ferme sans effet (surface seule, choix B2.6) ;
- rectangle, cercle et arc = points FIXES : ni ajout ni suppression
  (message explicite), seuls les deplacements sont permis ;
- round-trip save/load verifie pour les 4 formes ; total **350,
  0 echec**.

### B2.7e E3 : extrusions editables (profil + hauteur)

- **P sur une feuille issue d un profil = extrusion PARAMETRIQUE** :
  l objet passe en type 8 (EXTRUSION) - il garde le profil (sous-type
  + points de definition + ferme/surface) et le vecteur d extrusion ;
  la feuille est consommee (la face devient le solide, fidele SketchUp) ;
- regen par les MEMES fonctions que le trace (chaines rectangle/cercle/
  arc/spline, fan) puis prismatage : murs + caps bas/haut, normales
  exterieures quel que soit le sens du vecteur (tirer OU pousser) ;
- edition (E) : poignees du profil au sol (drag = le solide suit) +
  poignee de HAUTEUR (bleue) au centre du cap superieur - drag dans un
  plan vertical face camera ; chiffres non necessaires ;
- profils ouverts extrudes = bande (murs seuls) : accessible par
  O (ouvert/ferme) a chaud sur une extrusion de ligne ; ajout/
  suppression de points selon les regles du profil (rect/cercle/arc
  = points fixes, polygone/ligne = garde au minimum) ;
- clic sur une FACE du solide = point ajoute au profil (projection
  verticale du point touche) ; clic-sol garde (pres du profil
  uniquement, fini les points parasites) ;
- B2.7e3c : un point ajoute EXACTEMENT sur un bord du profil (clic
  sur un mur) rendait un tri de cap COLINEAIRE (normale nulle) et
  bloquait le Push/Pull ("Face non plane") - pushFanTri ecarte tout
  tri de fan de surface nulle (fans types 1/7/8, cas plat + caps,
  les murs ne sont pas concernes) ; total **394, 0 echec** ;
- persistence : entete trace etendue (profil + vecteur) ; vieux
  projets relisibles.

### B3 : balayage (extrusion le long d'un chemin) - lot 1

- **Maj+W / bouton "Balayer" / menu Outils** : clic 1 = la FACE a balayer
  (feuille tracee), clic 2 = le FIL chemin (ligne, spline, arc) ;
  Echap ou clic droit = annulation ;
- type 9 : l'objet porte le profil (sous-type + points, comme type 8)
  ET le chemin (sous-type + points) ; le fil chemin est **consomme**
  (retire de la scene, ses points restent dans les parametres - une
  fonction "extraire le chemin" est notee backlog B5+) ;
- regen = anneaux du profil transportes le long du chemin (transport
  parallele projete : pas de torsion en v1) ; profil perpendiculaire
  au depart du chemin ; caps aux extremites si le profil est plein ;
  chemin ferme (boucle) = solide torique sans caps ; orientation
globale corrigee par le volume signe (normales exterieures) ;
- refus explicites : face non tracee/coque, chemin non fil, meme
  objet, fil trop court ;
- persistence : entete trace etendue au type 9 (profil + normale +
  chemin), vieux projets relisibles ; edition du balayage = lot 2
  (message dedie en attendant) ;
- **B3.2 (retours test)** : (1) **point de connexion maitrise** -
  l'ancre du balayage = le point de la face le plus proche du DEPART
  du fil, pose exactement sur le chemin (fidele Follow Me : la
  feuille ou on la pose decide du resultat) ; (2) les refus du
  balayage s'affichent AUSSI dans le pied de page (flashBand) ;
  (3) **la ligne L ferme en BOUCLE (fil), JAMAIS en surface** -
  fermer en surface est le role du POLYGONE (difference ligne /
  polygone) ; Maj+L = fil ouvert ; les vieux projets avec feuille de
  ligne fermee restent lus (regen type 1 conserve la branche
  surface) ; total **412, 0 echec**.

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
