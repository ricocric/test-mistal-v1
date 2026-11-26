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
- B4 offset + chanfrein
- B5 edition directe : sommets / aretes / faces, scinder, pont
- B6 booleens (union / intersection / soustraction)
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
