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
- **Caméra** : clic-glisser dans le vide = orbite, molette = zoom, clic droit = panoramique
- **Aimantation** (`G`) : alignement sur la grille (pas de 0,5)
- **Palette** : couleur des nouveaux objets, recolorer la sélection
- **Ctrl+Z** : annuler (ajout, suppression, déplacement, extrusion, couleur)
- **Enregistrer / Ouvrir** : modèle au format `.json`
- **📤 OBJ** : export de la géométrie en `.obj` (Blender, SketchUp, 3ds Max…)
- **🖨️ STL** : export `.stl` pour impression 3D

## Technologies

- [Three.js](https://threejs.org/) (module ES, import map) — rendu WebGL, ombres, grille, contrôles orbite
- HTML/CSS/JS vanilla, aucune dépendance côté serveur
