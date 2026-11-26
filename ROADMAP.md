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
  etirable au push/pull ; chiffres L,H + Entree = dimensions exactes
  orientees par le quadrant du curseur ; clic droit/Echap = annule ;
- B2.5 FERMETURE DE LIGNE fait (avant B2.2-B2.4, demande utilisateur) :
  clic sur le depart (>= 3 sommets, tolerance filPickTol) = feuille
  fusionnee au fil en UN objet (fan 0,i,i+1, etirable au push/pull) ou fil
  ouvert en boucle (arete n-1 -> 0 ajoutee apres la derive) ; B2.5b (retour
  de test) : choix par la COMMANDE d'armement, rien de memorise - Outils >
  Ligne (fermee en surface) = L, Outils > Ligne (fil ouvert) = Maj+L ;
  correction au passage d'un bug de relocalisation (v locales, pos + v =
  monde) ; la derive du fan compte la diagonale (5 aretes pour un carre) -
  se rattrapera avec le vrai solide de B6 ;
- B2.2 RECTANGLE (conception avant code, mecanique commune B2) :
  - clic 1 = coin, souris = apercu du rectangle au sol, clic 2 = pose ;
    clic droit ou Echap = annulation SANS objet pose ; Entree vide = rien ;
  - v1 aligne aux axes (largeur en X, profondeur en Z), pose au sol y = 0 ;
    l'angle libre attendra l'inference de pose de B5 ;
  - objet pose = feuille pleine de 2 tris + aretes de bord, UN objet
    (coherent avec la ligne fermee B2.5), etirable au push/pull ;
  - saisie exacte : chiffres + Entree = "L,H" (largeur, profondeur) ;
    le quadrant du curseur par rapport au premier coin oriente le rectangle
    (fidele SketchUp : la diagonale dirige) ;
  - nommage "Rectangle", "Rectangle (2)"... ; bouton + entree menu Outils ;
  - touche : R est pris (Tourner) -> Maj+R propose ;

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
- Deplacements aimantes / collisions (T2), groupes d'objets (T3)
- Points de pivot choisis, mesures / cotes magnetees
- Fermeture avec confirmation "projet modifie, enregistrer ?"

## Idees a ne pas oublier (etudiees plus tard)

- Connexion OpenStreetMap
- Photogrametrie
- Modelisation a partir d'un jeu de photos
- Redaction d'une API pour chaque fonction creee
- Langage de script
