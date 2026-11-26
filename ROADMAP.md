# Roadmap Sketch3D natif

Backlog des phases de developpement. Reference : SketchUp - rien dans les
fioritures, tout dans la simplicite et l'efficacite. Regle adoptee : le
backlog est revu a chaque franchissement d'etape importante.

## Phase A - outillages (TERMINEE)

- Export cible / multi, suppression multi, compteur, Ctrl+S (T1.6.x)
- Multi-selection Ctrl+clic + actions multi (T1.7) + durcissement crash
- Point de vue : pan molette/Shift, recentrage C (T1.8)
- Rotation multi cas 1 : axes propres (T1.9) + correctif gel bandeau (T1.9.1)
- Rotation multi cas 2 : barycentre du groupe, bascule B a chaud (T1.10)
- Deplacement de groupe : drag multi (T1.11)
- Copier-coller Ctrl+C / Ctrl+V (T1.12)

## Phase B - l'usine (construction d'objets)

- **B0 fichier de setup** : `sketch3d.ini` a cote de l'exe, sauvegarde a la
  fermeture + a chaque changement de preference
- **B1 barre d'outils flottante v1** : plein de boutons pour choisir
  rapidement un outil (mode exclusif, identique aux raccourcis)
- B2 dessin de profils : ligne / rectangle / cercle / arc / polygone sur une
  face ou le sol
- B3 extrusion chemin / revolution / follow-me

### B2 - conception (posee AVANT code, revue au premier lot)

Mecanique commune aux 5 profils (meme contrat que le reste de l'application,
"validation = pose + sortie du mode", saisie chiffres dans le bandeau) :

- clic 1 = depart, souris = aperçu portable en continu, clic 2 (ou chiffres
  + Entree = dimension exacte) = pose ; clic droit ou Echap = annulation
  SANS objet pose ;
- v1 : pose sur le plan du sol (y = 0 monde) ; poser sur une face viendra
  apres (depends du picking face-a-face de B5) ;
- chaque profil pose un objet INDEPENDANT en tri-soup, coherent avec le
  push/pull et l'extrusion existants (stock brut exploite par B3) ;
- petits lots dans l'ordre : B2.1 ligne, B2.2 rectangle, B2.3 cercle,
  B2.4 arc, polygone (numero a revoir a la revue du backlog) ; boutons
  ajoutes a la barre flottante ;
- B2.1 LIGNE fait : fil multi-segments en UN objet (clics = sommets lies,
  clic droit = fin - exception validee a la regle "clic droit = neutre",
  Entree vide = fin, chiffres+Entree = longueur exacte puis pose, Echap =
  tout annule) ; chaine d'aretes derivee pour objets sans triangle
  (computeObjectDerived) ; menu Outils cree (regle B1.7c) ;
- B2.2 RECTANGLE fait : Maj+R, clic coin/clic coin oppose, feuille pleine
  de 2 tris normale +y (coherente avec la ligne fermee B2.5), UN objet
  etirable au push/pull ; chiffres L;H + Entree = dimensions exactes (B2.2b : separateur
  point-virgule, la virgule proscrite - confusion decimale)
  orientees par le quadrant du curseur ; clic droit/Echap = annule ;
- B2.2c fait (retour de test) : garde makeExtrude relachee a 1 tri complet
  (une feuille de 2 tris est une face push/pull valide) ; clavier FR :
  ';' NON majuscule sur AZERTY -> la saisie utilise l'etiquette reelle de
  la touche (glfwGetKeyName), ce qui est tape s'affiche ; convention
  « ; » = separateur, « , »/« . » = decimales ;
- B2.3 CERCLE fait : Maj+C, clic centre + clic rayon (apercu cercle +
  segment de rayon), feuille fan 24 tris normale +y etirable au push/pull,
  chiffres + Entree = rayon exact (un seul nombre, decimales FR) ;
  B2.3b (retour de test) : chiffres du pave retablis (tout etait remplace
  par la decimale) ; note :
  le moteur de tests compte desormais ses CHECKs executes (resume :
  "N echec(s), M verifications") - les totaux anterieurs etaient des
  comptages de lignes, sous-estimes ;
- B2.4 ARC fait : « Arc 2 points » de SketchUp (depart, fin de corde,
  bombement pose au 3e clic, apercu arc flechissant), 12 segments, FIL
  OUVERT (choix utilisateur, completable par une ligne de 2 points) ;
  points recalcules EXACTS sur le cercle des 3 clics (mediatrices) ;
  saisie corde puis fleche (decimales FR) ; Maj+A ; B2.4d : fleche libre
  (arc majeur > 180 deg, proche du cercle complet) ;
  B2.4b (retour de test, grave) : g_toolBtn[9] debordait (11 boutons
  crees) = corruption memoire au demarrage ; [11] + taille de toolLayout
  derivee du tableau (synchro automatique) ;
  B2.4c (retour de test) : AdjustWindowRect dans toolLayout (la barre de
  titre rognait la derniere rangee : Rectangle/Cercle/Arc invisibles
  depuis B2.3) ; SetFocus apres chaque commande menu/bouton (le clic
  volait le focus clavier a la vue 3D) ; gardes du dispatcher verbeuses ;
  B2.4d (retour de test) : limite des 180 deg levee - fleche libre,
  arc majeur (> 180 deg) jusqu'au proche-cercle ; +2 verifications = 278 ;
  B2.4e (retour de test) : Maj+A mort = GLFW rapporte les touches par
  POSITION QWERTY (AZERTY : A arrive en GLFW_KEY_Q) ; raccourcis lettres
  desormais compares par ETIQUETTE reelle (keyIsLetter/glfwGetKeyName,
  regle B2.2d), repli par code ;
  B2.4f (regle generalisee, demande utilisateur) : TOUTE comparaison de
  touche lettre passe par keyIsLetter (dernier cas : pivot B de la
  rotation) ; touches de controle par code (positions universelles) ;
  stub : disposition AZERTY simulee pour (GLFW_KEY_Q, sc 0x10) = "A",
  +2 verifications executees = 280, 0 echec ;
- B2.6 POLYGONE QUELCONQUE fait (conception avant code, choix utilisateur :
  Maj+P, surface seule) : N clics = sommets au sol, fermeture = clic sur
  le depart (>= 3 sommets, tolerance filPickTol), feuille fan (0,i,i+1)
  en UN objet etirable au push/pull, aretes = bords + diagonales (v1) ;
  pas de saisie numerique v1 ; 12e bouton + menu Outils ; +8 verifications
  = 288, 0 echec ;
  B2.6b (retour de test : « disparait des que l'on sort par clic D ») :
  clic droit ou Entree = VALIDER la surface (comme le clic sur le depart,
  polyFinish), Echap SEUL sort sans rien poser ; message « au moins
  3 sommets » si validation prematuree ; +2 verifications = 290, 0 echec ;
  PHASE B2 (profils de ligne) COMPLETE ;
- B2.5 FERMETURE DE LIGNE fait (avant B2.2-B2.4, demande utilisateur) :
  clic sur le depart (>= 3 sommets, tolerance filPickTol) = feuille
  fusionnee au fil en UN objet (fan 0,i,i+1, etirable au push/pull) ou fil
  ouvert en boucle (arete n-1 -> 0 ajoutee apres la derive) ; B2.5b (retour
  de test) : choix par la COMMANDE d'armement, rien de memorise - Outils >
  Ligne (fermee en surface) = L, Outils > Ligne (fil ouvert) = Maj+L ;
  correction au passage d'un bug de relocalisation (v locales, pos + v =
  monde) ; la derive du fan compte la diagonale (5 aretes pour un carre) -
  se rattrapera avec le vrai solide de B6 ;
- REGLE DES VARIANTES (B2.5b, generalisee B2.7b, validation
  utilisateur) : tout choix de conception a deux usages = variantes
  COMMUTABLES par la commande d'armement, rien de memorise ; bascule a
  chaud quand les points survivent au changement (spline Maj+S/Maj+B) ;
- B2.7 SPLINE fait (conception avant code, DERNIER TRACE DE BASE, choix
  utilisateur : Maj+S, points de PASSAGE - reserve de tester les points
  de controle plus tard -, entree unique, fil seul) : courbe cubique C1
  passant par chaque clic ; parametrisation centripete (sqrt des cordes,
  tangentes Barry-Goldman) = robuste aux espacements irreguliers (pas de
  boucle ni de pic) ; clic = point (doublon ignore) ; clic sur le depart
  (>= 3 points, tolerance filPickTol) = boucle FERMEE lissee ; clic droit
  ou Entree (pave inclus) = VALIDER la courbe ouverte (>= 2 points,
  regle B2.6b) ; Echap = sortir sans rien poser ; fil discretise (16
  sous-segments par intervalle, v1 fixe) en UN objet sans triangle,
  ancre locale = premier point, aretes = chaine derivee (boucle fermee :
  sommet final = depart, arete de cloture naturelle), etirable au
  deplacement (filRayHit) ; apercu = courbe lisse + rubber band droit
  (prolongement droit en v1) ; pas de saisie numerique v1 ; 13e bouton +
  menu Outils + entete console ; +9 verifications = 299, 0 echec ;
  options futures (reserve utilisateur) : interpolation globale type
  NURBS (A9.1) ;
  B2.7b (retour de test : « j'aurai du prendre tout de suite le choix
  points de controle ») : variante COMMUTABLE - Maj+S = passage,
  Maj+B = points de controle (les clics tracent le polygone de controle,
  la courbe glisse a l'interieur, noeuds serres = depart et arrivee
  exacts si ouverte, boucle periodique si fermee) ; bascule Maj+S/Maj+B
  A CHAUD pendant un trace, points conserves ; polygone de controle
  visible en apercu ; 14e bouton + 2e entree menu Outils ; +5
  verifications = 304, 0 echec ;
  B2.7c : menu Outils groupe par barres horizontales (variantes isolees,
  demande utilisateur) ;
  B2.7d (retour de test : « 2 segments de droite » en points de
  controle) : BUG du span - floor(u) est FAUX sur les noeuds serres
  (repetes aux extremites), la courbe ouverte se reduisait a du
  rectiligne ; correctif = findSpan (NURBS Book A2.1) pour l'ouvert,
  floor(u) garde pour le ferme (noeuds uniformes) ; check renforce
  (valeur EXACTE de Bezier au milieu du carre de controle, aurait
  attrape le bug) ; mode d'emploi enrichi (message d'armement +
  bandeau) ; +1 verification = 305, 0 echec ; VALIDATION UTILISATEUR :
  « la seconde version est plus proche de ce que j'avais en tete » (les
  deux variantes restent, regle des variantes) ;
- BACKLOG B2.7e (demande utilisateur) : EDITION DE SPLINE APRES COUP -
  l'objet pose ne conserve aujourd'hui que la courbe discretisee (points
  de controle perdus) ; prevoir de stocker dans l'objet : points de
  controle + variante (passage/controle) + ouvert/ferme, pour
  re-editer la courbe (deplacer/ajouter/supprimer des points) apres la
  pose ; conception AVANT code (lien avec le vrai solide B6 et avec
  l'edition libre de maillage) ;
- B2.2 RECTANGLE (conception avant code, mecanique commune B2) :
  - clic 1 = coin, souris = apercu du rectangle au sol, clic 2 = pose ;
    clic droit ou Echap = annulation SANS objet pose ; Entree vide = rien ;
  - v1 aligne aux axes (largeur en X, profondeur en Z), pose au sol y = 0 ;
    l'angle libre attendra l'inference de pose de B5 ;
  - objet pose = feuille pleine de 2 tris + aretes de bord, UN objet
    (coherent avec la ligne fermee B2.5), etirable au push/pull ;
  - saisie exacte : chiffres + Entree = "L;H" (largeur, profondeur) ;
    le quadrant du curseur par rapport au premier coin oriente le rectangle
    (fidele SketchUp : la diagonale dirige) ;
  - nommage "Rectangle", "Rectangle (2)"... ; bouton + entree menu Outils ;
  - touche : R est pris (Tourner) -> Maj+R propose ;

- B2.3 CERCLE (conception avant code, mecanique commune B2, choix
  utilisateur : touche Maj+C) :
  - clic 1 = centre (au sol y = 0), souris = apercu (contour jaune +
    segment de rayon du centre au curseur), clic 2 = pose ;
    clic droit ou Echap = annulation SANS objet pose ;
  - chiffres + Entree = rayon exact (UN seul nombre, decimales FR acceptees
    : ',' et '.' = decimales, convention B2.2b) ; direction : le curseur
    donne l'orientation (comme le rectangle) puis pose + sortie du mode ;
  - objet pose = feuille fan de 24 tris (centre + boucle de 24 sommets,
    25 sommets au total), normale +y, UN objet, etirable au push/pull
    (garde makeExtrude = 1 tri depuis B2.2c) ; 24 segments fixes en v1 ;
  - saisie : etiquette reelle de la touche pour la decimale (B2.2d,
    glfwGetKeyName) - meme contrat que le rectangle ;
  - nommage "Cercle", "Cercle (2)"... ; bouton + entree menu Outils
    "Cercle  Maj+C" (regle B1.7c) ; 10e bouton de la barre (toolLayout n=10) ;

- B2.4 ARC (conception avant code, mecanique commune B2, choix
  utilisateur : interaction « Arc 2 points » de SketchUp, objet = FIL
  OUVERT) :
  - 3 clics : clic 1 = depart (P1, au sol), clic 2 = fin (P2, la corde),
    souris = bombement, clic 3 = pose ;
  - les 3 points (P1, bombement B, P2) definissent LE cercle unique
    (centre = intersection des mediatrices, rayon) ; l'arc pose est la
    portion de P1 a P2 PASSANT PAR B, points recalculés exacts sur le
    cercle ; B2.4d : angle balaye LIBRE (< 360 deg) - fleche > moitie de
    corde = arc majeur, fleche 5-10x la corde = proche du cercle ;
  - objet pose = fil ouvert (choix utilisateur) : N+1 points, N segments,
    SANS triangle (chaîne derivee comme la ligne B2.1 : capture filRayHit,
    hors exports OBJ/STL) ; completable apres coup par une ligne de 2
    points pour fermer une surface etirable (remarque utilisateur) ;
    le push/pull d'un arc plein passera par la fermeture ou par B3 ;
  - N = 12 segments fixes en v1 (defaut SketchUp arc ; le cercle garde 24)
    - a confirmer avec l'utilisateur ;
  - saisie exacte en deux temps (meme contrat que la ligne) : apres le
    depart, chiffres + Entree = longueur de corde exacte (direction =
    curseur) pose la fin ; apres la fin, chiffres + Entree = fleche exacte
    (bombement, cote = curseur) pose l'arc ; decimales FR (convention
    B2.2b/B2.2d) ;
  - Maj+A arme (retour = desarme) ; Echap/clic droit = annulation SANS
    objet ; nommage "Arc", "Arc (2)"... ; bouton + entree menu Outils
    (11e bouton, regle B1.7c) ; zone de saisie "corde" puis "fleche" ;

Questions ouvertes a trancher avec l'utilisateur :
- ligne : objet "fil" (2 sommets, GL_LINES, pas de volume -> pas de
  push/pull) OU feuille fine ? (retenu : fil de la v1, c'est le chemin que
  B3 multipliera) ;
- arc : 3 clics SketchUp (centre, debut, fin) retenus ;
- cercle : 24 segments fixes en v1 (preferences/INI plus tard) ;
- polygone : N clics, Entree ou double-clic ferme, triangulation en eventail,
  refuse en dessous de 3 sommets ;
- menu Outils : cree avec B2 (les 5 nouveaux outils justifient un menu
  propre, raccourcis rappeles dans le menu - regle B1.7c) ;
- B4 offset + chanfrein
- B5 edition directe : sommets / aretes / faces, scinder, pont
- B6 booleens (union / intersection / soustraction) - DECISION utilisateur :
  le tri-soup (soupe de triangles, fidele au web) suffira jusqu'ici, mais un
  VRAI SOLIDE doit naitre au plus tard avec B6 (topologie structuree type
  demi-aretes : faces, aretes partagees, voisinage) - sans topologie, les
  booleens risquent de devenir des rustines ; B5 (edition directe, notion de
  face) et B4 (offset/chanfrein) tireront aussi parti de cette migration
- B7 deformations : echelle non homogene, twist, bend, taper
- B8 pivot intelligent : ETUDE DE CONCEPTION avant code (nombreux cas de
  figure : sommet, face, point clique, coordonnees, centre du monde...)

## Phase C - interface (ImGui)

- Toolbars flottantes dockables, 2 ecrans, cotes, unites

## Phase D - rendu

- PBR par shaders (passage fixed-function -> GLSL, contexte moderne)
- Rendus photo / video (FFmpeg)

## Phase E - formats avances

- Assimp (formats BSD)
- .skp SDK Trimble
- CSV

## Lignes ouvertes (a replacer dans un lot)

- Ctrl+X (couper) et collage au point clique (lie au sujet pivot B8)
- Mode persistance (rejeu des commandes effectuees) et mode macro (script ->
  commandes), plus tard : ce sont des PRODUCTEURS DE COMMANDES qui passeront
  par le dispatcher runToolCommand - prerequis d'homogeneite pose en B1.5
  (identifiants de commande contigus, un seul chemin de code) ;
- Menu Preferences a cote de Fichier : editer les choix au vol (formats
  d'export, preferences d'outils, unites...) - le mecanisme B0 ([outils] et
  [exports] sauvegardes a chaque changement) est deja pret pour l'accueillir ;
  en attente : demande utilisateur (phase B)
- Pied de page = infos de la COMMANDE EN COURS uniquement (regle B1.7c) :
  chaque raccourci est rappele dans le menu qui le propose (ex. Enregistrer
  Ctrl+S) ; reste a faire : un menu outils (P extraire, R tourner, C centrer,
  F cadrer, H origine) qui portera leurs raccourcis, aujourd'hui rappeles
  seulement dans l'entete console et les boutons de la barre flottante ;
- STL ASCII editable (informationnel), export fiable par fichier
- Surface -> fil : fonction qui part d'une surface quelconque (feuille ou
  solide) et la transforme en fil (arêtes de contour / wireframe) - demande
  utilisateur B2.5b ("il sera probablement utile de creer une fonction qui
  part d'une surface quelconque et la transforme en fil") ; utile pour
  l'edition de profils (B3 chemins), l'edition directe (B5) et la lecture
  des structures (se rattachera naturellement a la vraie topologie de B6 :
  tri des aretes de bord = utilisees par UNE seule face) ;
- Deplacements aimantes / collisions (T2), groupes d'objets (T3)
- Points de pivot choisis, mesures / cotes magnetees
- Fermeture avec confirmation "projet modifie, enregistrer ?"

## Idees a ne pas oublier (etudiees plus tard)

- Connexion OpenStreetMap
- Photogrametrie
- Modelisation a partir d'un jeu de photos
- Redaction d'une API pour chaque fonction creee
- Langage de script
