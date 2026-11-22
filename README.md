# Sketch3D — dessin 3D façon SketchUp

Application web de dessin 3D inspirée de SketchUp, en un seul fichier `index.html`.

## Lancer

Ouvrir `index.html` dans n'importe quel navigateur moderne (Three.js est chargé via CDN, connexion internet requise). Aucun serveur ni installation nécessaire.

## Outils

| Outil | Raccourci | Usage |
|---|---|---|
| Sélection | `S` | Clic pour sélectionner, glisser pour déplacer, `Suppr` pour supprimer |
| Rectangle | `R` | Clic-glisser au sol pour tracer la base |
| Cercle | `C` | Clic au sol (centre), puis clic pour valider le rayon |
| Push/Pull | `P` | Cliquez une **face** (dessus = hauteur, côté = largeur/profondeur), déplacez la souris, puis **re-cliquez** pour la poser (clic droit / `Échap` = annuler) |
| Orbite | `O` | Rotation de la caméra |

## Autres commandes

- **Cube / Cylindre / Sphère** : pose rapide au point cliqué
- **Caméra** : clic-glisser dans le vide = orbite, molette ou boutons 🔍+ / 🔍± = zoom, clic droit = panoramique ; **🎯 Cadrer** (`F`) ajuste la vue sur la sélection ou toute la scène
- **Aimantation** (`G`) : alignement sur la grille (pas de 0,5)
- **Palette** : couleur des nouveaux objets, recolorer la sélection
- **Ctrl+Z** : annuler (ajout, suppression, déplacement, extrusion, couleur)
- **Clic droit sur un objet** : menu contextuel — Dupliquer… (nombre de copies au choix, alignées en ligne), Supprimer, Cadrer ; **Ctrl+D** duplique la sélection
- **Menu Fichier** : ouvrir/enregistrer un projet (`.json` — y compris la géométrie des objets importés), importer un objet (`obj`/`stl`), exporter en OBJ ou STL, fermer le projet, quitter l'application
- **Import/Export OBJ** : géométrie avec normales, groupes par objet (Blender, SketchUp, 3ds Max…) ; objets importés visibles (matériau double face) et auto-redimensionnés s'ils sortent de l'échelle de la scène
- **Import/Export STL** : binaire ou ASCII à l'import, ASCII à l'export — impression 3D

## Technologies

- [Three.js](https://threejs.org/) (module ES, import map) — rendu WebGL, ombres, grille, contrôles orbite
- HTML/CSS/JS vanilla, aucune dépendance côté serveur
