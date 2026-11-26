# Bloc-notes du projet (trace des segments importants)

Fichier de notes maintenu a la main (moi sur demande, vous directement) ;
versionne avec le depot : toujours disponible apres `git fetch`.

---

## Formats et dimensions des boutons / icones de barres d'outils (Windows, Win32)

### Formats de fichiers
- **Source de travail** : PNG 32 bits (alpha) ou SVG pour dessiner ;
- **Integration Win32** :
  - `ImageList_Create` + `ImageList_Add` (strip BMP 32 bits ARGB ou PNG
    charge via GDI+) puis `TB_SETIMAGELIST` si la barre devient une
    vraie toolbar Win32 (cas des barres dockees plus tard) ;
  - boutons actuels = `BUTTON` Win32 texte -> pour icone + texte, un
    `BS_BITMAP`/`BS_ICON` + `BM_SETIMAGE`, ou owner-draw plus tard ;
  - `.ico` multi-resolutions reserve a l'exe (icone d'application).

### Dimensions classiques (par bouton)
| Usage              | Taille             |
|--------------------|--------------------|
| Petite icone       | 16 x 16            |
| Moyenne (VS clas.) | 24 x 24 (recommande ici) |
| Grande             | 32 x 32            |
| DPI 125/150 %      | prevoir 20/24/30/36/48 |

- Boutons actuels : 86 x 26 px -> icone **16 ou 24 px + texte** a cote ;
- 1 a 2 px de marge interne, fond transparent (alpha), trait 1 a 1.5 px
  a 16 px (2 px a 24 px) ; pas de texte dans l'icone ;
- style conseille : silhouette plate, 1-2 couleurs, lisible sur fond
  clair ET sombre ;
- garder un calque source par icone (un fichier par outil) pour
  regenerer les tailles.

### Regles projet a rappeler
- Regle des variantes (B2.5b/B2.7b) : choix a deux usages = variantes
  commutables par la commande, rien de memorise ;
- Raccourcis : Maj + lettre pour les formes de base (Maj+R, Maj+C,
  Maj+A, Maj+P, Maj+S passage, Maj+B controle) ; etiquette reelle via
  keyIsLetter (JAMAIS le code GLFW brut pour une lettre) ;
- Saisie : « ; » separateur, « , » et « . » decimales ;
- PowerShell 5.x : pas de « && », une commande par ligne ;
- Sequence de test : `git fetch origin` + `git reset --hard
  origin/<branche>` + `.\go.ps1` ; titre de fenetre = preuve de build.
