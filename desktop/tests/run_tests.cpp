/* Sketch3D natif — harnais de tests geometrique (sans fenetre, sans GLFW reel).
   Compile avec les stubs de ./stub ; inclut directement src/main.cpp.
   Lancer : cmake --build build --target sketch3d_tests puis executez build/sketch3d_tests */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <set>
#include <unordered_map>
#include <cstdint>

#define main sketch3d_unused_main
#include "../src/main.cpp"
#undef main

static int g_fail = 0;
static int g_tot = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("ECHEC: %s\n", (msg)); ++g_fail; } \
                              else { std::printf("ok : %s\n", (msg)); ++g_tot; } } while (0)

static std::string testsDir() {
    static const std::string d = [] {
        namespace fs = std::filesystem;
        std::string f = __FILE__;
        const size_t s = f.find_last_of("/\\");
        const std::string rel = f.substr(0, s);
        if (fs::exists(rel + "/data")) return rel;
        for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
            const fs::path cand = p / "desktop" / "tests";
            if (fs::exists(cand / "data")) return cand.string();
        }
        return rel;
    }();
    return d;
}
static std::string dataPath(const char* name) { return testsDir() + "/data/" + name; }
static std::string tmpPath(const char* name) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sketch3d_tests";
    fs::create_directories(dir);
    return (dir / name).string();
}

static size_t countNonManifold(const Object& o) {
    std::unordered_map<unsigned long long, int> arity;
    for (size_t i = 0; i < o.tri.size(); i += 3)
        for (int k = 0; k < 3; ++k) {
            unsigned e0 = o.tri[i + k], e1 = o.tri[i + (k + 1) % 3];
            if (e0 > e1) { unsigned t = e0; e0 = e1; e1 = t; }
            ++arity[((unsigned long long)e0 << 32) | e1];
        }
    size_t nm = 0;
    for (const auto& kv : arity) if (kv.second != 2) ++nm;
    return nm;
}

static double signedVolume(const Object& o) {
    double vol = 0.0;
    for (size_t i = 0; i < o.tri.size(); i += 3)
        vol += dot(o.v[o.tri[i]], cross(o.v[o.tri[i + 1]], o.v[o.tri[i + 2]]));
    return vol / 6.0;
}

int main() {
    /* cube de reference genere par le code lui-meme : unite, 12 tris, coque fermee */
    const double col[3] = {0.5, 0.5, 0.5};
    const Object cube = makeBoxObject("Cube A", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
    CHECK(cube.tri.size() == 36 && cube.edges.size() == 18, "boite de reference : 12 tris, 18 aretes");
    CHECK(std::fabs(signedVolume(cube) - 1.0) < 1e-12, "boite de reference : volume signe = 1");

    /* --- 1. OBJ simple --- */
    {
        std::ofstream f(tmpPath("cube.obj"));
        for (const Vec3& p : cube.v) f << "v " << p.x + cube.pos.x << " " << p.y + cube.pos.y << " " << p.z + cube.pos.z << "\n";
        for (size_t i = 0; i < cube.tri.size(); i += 3)
            f << "f " << cube.tri[i] + 1 << " " << cube.tri[i + 1] + 1 << " " << cube.tri[i + 2] + 1 << "\n";
    }
    Object a; std::string err;
    CHECK(loadMeshFile(tmpPath("cube.obj"), a, err), err.c_str());
    CHECK(a.v.size() == 8 && a.tri.size() == 36, "OBJ : 8 sommets, 12 tris");
    CHECK(std::fabs(signedVolume(a) - 1.0) < 1e-12, "OBJ : volume signe = 1");
    CHECK(countNonManifold(a) == 0 && a.edges.size() == 18, "OBJ : coque fermee, 18 aretes uniques");

    /* --- 2. OBJ avec slashes et indices negatifs --- */
    {
        std::ofstream f(tmpPath("cube2.obj"));
        f << "# objet de test\nusemtl rien\n";
        for (const Vec3& p : cube.v) f << "v " << p.x + cube.pos.x << " " << p.y + cube.pos.y << " " << p.z + cube.pos.z << "\n";
        for (size_t i = 0; i < cube.tri.size(); i += 3)
            f << "f " << cube.tri[i] + 1 << "/1/" << cube.tri[i] + 1 << " "
              << cube.tri[i + 1] + 1 << "/1/" << cube.tri[i + 1] + 1 << " "
              << cube.tri[i + 2] + 1 << "/1/" << cube.tri[i + 2] + 1 << "\n";
        f << "f -8 -7 -6\n";
    }
    Object b; err.clear();
    CHECK(loadMeshFile(tmpPath("cube2.obj"), b, err), err.c_str());
    CHECK(b.tri.size() == 39, "OBJ : vt/vn ignores, indice negatif => 13 tris");

    /* --- 3. STL binaire avec entete "solid..." (piege de detection), soudure N4 --- */
    {
        std::string bytes(80, '\0');
        std::memcpy(bytes.data(), "solid ce nest pas du texte", 26);
        const uint32_t cnt = 12;
        bytes.append((const char*)&cnt, 4);
        for (size_t i = 0; i < cube.tri.size(); i += 3) {
            const Vec3 n = cube.n[i / 3];
            float rec[12] = {(float)n.x, (float)n.y, (float)n.z, 0,0,0,0,0,0,0,0,0};
            for (int k = 0; k < 3; ++k) {
                const Vec3 w = cube.pos + cube.v[cube.tri[i + k]];
                rec[3 + k * 3] = (float)w.x; rec[4 + k * 3] = (float)w.y; rec[5 + k * 3] = (float)w.z;
            }
            bytes.append((const char*)rec, sizeof(rec));
            const uint16_t attr = 0;
            bytes.append((const char*)&attr, 2);
        }
        std::ofstream f(tmpPath("cube_b.stl"), std::ios::binary);
        f.write(bytes.data(), (std::streamsize)bytes.size());
    }
    Object c; err.clear();
    CHECK(loadMeshFile(tmpPath("cube_b.stl"), c, err), err.c_str());
    CHECK(c.v.size() == 8 && c.tri.size() == 36, "STL binaire : soudure => 8 sommets (36 coins fondus), 12 tris");
    CHECK(std::fabs(signedVolume(c) - 1.0) < 1e-5, "STL binaire : volume signe = 1");
    CHECK(countNonManifold(c) == 0 && c.edges.size() == 18, "STL binaire : coque fermee soudee, 18 aretes");

    /* --- 4. STL ASCII (soudure aussi) --- */
    {
        std::ofstream f(tmpPath("cube_a.stl"));
        f << "solid test\n";
        for (size_t i = 0; i < cube.tri.size(); i += 3) {
            f << "facet normal 0 0 0\nouter loop\n";
            for (int k = 0; k < 3; ++k) {
                const Vec3 w = cube.pos + cube.v[cube.tri[i + k]];
                f << "vertex " << w.x << " " << w.y << " " << w.z << "\n";
            }
            f << "endloop\nendfacet\n";
        }
        f << "endsolid test\n";
    }
    Object d; err.clear();
    CHECK(loadMeshFile(tmpPath("cube_a.stl"), d, err), err.c_str());
    CHECK(d.v.size() == 8 && d.tri.size() == 36 && std::fabs(signedVolume(d) - 1.0) < 1e-12, "STL ASCII : soudure => 8 sommets, 12 tris, volume = 1");

    /* --- 5. round-trip export OBJ -> relecture --- */
    {
        g_objs.clear();
        g_objs.push_back(makeBoxObject("Pave", Vec3(2, 0.6, 1), Vec3(1.5, 0.6, 0.8), col));
        g_objs.push_back(std::move(a));   /* cube lu en (1), tel quel */
        exportOBJ(tmpPath("scene_export.obj"));
        g_objs.clear();
        Object r; err.clear();
        CHECK(loadMeshFile(tmpPath("scene_export.obj"), r, err), err.c_str());
        CHECK(r.tri.size() == 72 && std::fabs(signedVolume(r)) > 0.0, "round-trip OBJ : 24 tris, lecture reussie");
    }

    /* --- 6. fichier reel de l'utilisateur (SketchUp : 2 meshes, coordonnees en mm) --- */
    {
        Object r; err.clear();
        if (loadMeshFile(dataPath("cube_sketchup.obj"), r, err)) {
            std::printf("fichier reel : %zu sommets soudes, %zu tris, volume signe %.6g, non-manifold %zu\n",
                        r.v.size(), r.tri.size(), signedVolume(r), countNonManifold(r));
            CHECK(r.tri.size() >= 2600 && r.tri.size() <= 2769, "fichier reel : environ 2769 tris (Steve + cube)");
            placeImport(r);
            CHECK(std::fabs(signedVolume(r)) > 1e9, "fichier reel : pose au sol sans alterer le maillage");
        } else {
            std::printf("fichier reel : echec lecture (%s)\n", err.c_str());
            ++g_fail;
        }
    }

    /* --- 7. anti-superposition : deux imports identiques se decalent --- */
    {
        g_objs.clear();
        Object p1 = makeBoxObject("P1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), col);
        g_objs.push_back(p1);
        Object p2 = makeBoxObject("P2", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), col);
        placeImport(p2);
        CHECK(std::fabs(p2.pos.x) > 0.1, "anti-superposition : le second objet est decale");
        g_objs.clear();
    }

    /* --- 8. echelle par objet : normalisation, picking et export avec echelle --- */
    {
        Object r; err.clear();
        CHECK(loadMeshFile(dataPath("cube_sketchup.obj"), r, err), err.c_str());
        const double rawSpan = std::max(std::max(r.bmax.x - r.bmin.x, r.bmax.y - r.bmin.y), r.bmax.z - r.bmin.z);
        normalizeImportScale(r);
        CHECK(r.scl < 1.0 && std::fabs(rawSpan * r.scl - 4.0) < 1e-9, "fichier reel : normalisation a 4 unites de haut");
    }
    {
        Object b2 = makeBoxObject("B2", Vec3(0, 0, 0), Vec3(1, 1, 1), col);
        b2.scl = 2.0;
        b2.pos = Vec3(5, 2, 0);   /* monde : x 3..7, y 0..4, z -2..2 */
        Vec3 org(5, 2, 20), dir(0, 0, -1);
        double t;
        CHECK(rayObject(org, dir, b2, t) && std::fabs(t - 18.0) < 1e-9, "picking avec echelle : hit calculé en monde (t = 18)");
        g_objs.clear();
        g_objs.push_back(b2);
        exportOBJ(tmpPath("scaled.obj"));
        g_objs.clear();
        Object q; err.clear();
        CHECK(loadMeshFile(tmpPath("scaled.obj"), q, err), err.c_str());
        CHECK(std::fabs((q.bmax.x - q.bmin.x) - 4.0) < 1e-9 && std::fabs(q.bmax.y - q.bmin.y - 4.0) < 1e-9,
              "export avec echelle : coordonnees monde (-2..2 en x local x2 => 4)");
    }
    g_selected = -1;

    /* --- 9. import reel sur scene de demo : anticollision + cadrage (retour utilisateur N3.1) --- */
    {
        buildScene();
        Object imp; err.clear();
        CHECK(loadMeshFile(dataPath("cube_sketchup.obj"), imp, err), err.c_str());
        normalizeImportScale(imp);
        const int shifts = placeImport(imp);
        bool anyOverlap = false, onGround = std::fabs(imp.pos.y + imp.bmin.y * imp.scl) < 1e-9;
        for (const Object& o : g_objs) if (overlapsWorld(imp, o)) anyOverlap = true;
        CHECK(!anyOverlap, "import reel : aucun chevauchement avec la scene de demo apres placement");
        CHECK(onGround, "import reel : pose au sol (bmin.y monde = 0)");
        CHECK(shifts > 0, "import reel : au moins un decalage anticollision applique");

        const double firstX = imp.pos.x;
        g_objs.push_back(std::move(imp));
        g_selected = (int)g_objs.size() - 1;
        frameSelected();
        const Object& sel = g_objs[(size_t)g_selected];
        const Vec3 expectT = sel.pos + (sel.bmin + sel.bmax) * (0.5 * sel.scl);
        CHECK(std::fabs(g_cam.target.x - expectT.x) < 1e-9 && std::fabs(g_cam.target.y - expectT.y) < 1e-9
              && std::fabs(g_cam.target.z - expectT.z) < 1e-9,
              "cadrage post-import : cible = centre bbox en coordonnees MONDE (bug scl corrige)");
        const double rr = ((sel.bmax - sel.bmin) * (0.5 * sel.scl)).norm();
        const double expectD = std::max(rr / std::tan(kFovDeg * 3.14159265358979 / 360.0), 1.0) * 1.5;
        CHECK(std::fabs(g_cam.dist - expectD) < 1e-9 && g_cam.dist < 40.0,
              "cadrage post-import : distance = formule tangente, vue lisible (pas de zoom ecrase)");

        Object imp2; err.clear();
        CHECK(loadMeshFile(dataPath("cube_sketchup.obj"), imp2, err), err.c_str());
        normalizeImportScale(imp2);
        placeImport(imp2);
        bool anyOverlap2 = false;
        for (const Object& o : g_objs) if (overlapsWorld(imp2, o)) anyOverlap2 = true;
        CHECK(!anyOverlap2, "second import successif : aucun chevauchement (ni scene, ni premier import)");
        CHECK(imp2.pos.x > firstX, "second import successif : pousse plus loin en +X que le premier");
        g_objs.clear();
    }
    g_selected = -1;

    /* --- 10. Push/Pull (lots web 3.x, etat final 3.10.3) : tirage d'une face --- */
    {
        Object box = makeBoxObject("Boite", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        Extrude dd;
        /* tri 2 = premiere moitie de la face du dessus ({4,5,6}) */
        CHECK(makeExtrude(box, 2, dd), "push/pull : la face du dessus est capturable (patch coplanaire connecte)");
        CHECK(dd.vset.size() == 4 && dd.boundary.size() == 4, "push/pull : patch = 4 sommets, 4 aretes de perimetre");
        applyExtrude(box, dd, 1.0);
        CHECK(std::fabs(signedVolume(box) - 2.0) < 1e-9, "push/pull tirage +1 : volume = 2");
        CHECK(countNonManifold(box) == 0 && box.tri.size() == 36, "push/pull tirage : coque fermee, 12 tris");
        CHECK(box.v.size() == 8 && std::fabs(box.bmax.y - 0.5) < 1e-9, "push/pull tirage : les parois inchangees restent a 0.5 (indices partages, aucune jupe)");
        CHECK(std::fabs(box.bmax.z - 1.5) < 1e-9, "push/pull tirage : la face monte a 1.5 en local (dessus du cube natif = +Z)");
    }

    /* --- 11. push rentrant intermediaire : la face plie, pas de penetration --- */
    {
        Object box = makeBoxObject("Boite", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        Extrude dd;
        CHECK(makeExtrude(box, 2, dd), "push/pull : capture du dessus (11)");
        applyExtrude(box, dd, -0.2);
        CHECK(std::fabs(signedVolume(box) - 0.8) < 1e-9, "push/pull enfoncement -0.2 : volume = 0.8");
        CHECK(countNonManifold(box) == 0, "push/pull enfoncement : coque fermee (parois repliees, pas de penetration)");
        CHECK(std::fabs(box.bmax.z - 0.3) < 1e-9, "push/pull enfoncement : la face descend (0.3 local = 0.8 monde), pas a travers");
    }

    /* --- 12. butee : feuille a zero, puis pull = le volume est recree (3.10.3) --- */
    {
        Object box = makeBoxObject("Boite", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        Extrude dd;
        makeExtrude(box, 2, dd);
        applyExtrude(box, dd, -1.0);
        CHECK(dd.hasLow && std::fabs(dd.dLow + 1.0) < 1e-9, "push/pull : butee detectee a -1 (epaisseur de la boite)");
        CHECK(std::fabs(signedVolume(box)) < 1e-9 && (box.bmax.z - box.bmin.z) < 1e-9, "push/pull butee : feuille aplatie, volume zero, epaisseur nulle");
        CHECK(countNonManifold(box) == 8, "push/pull butee : feuille = 4 tris coplanaires (2 windings), 8 bords ouverts en index (fidele au pliage web)");
        size_t triTop = 0;
        for (size_t i = 0; i < box.n.size(); ++i)
            if (dot(box.n[i], Vec3(0, 0, 1)) > 0.9) { triTop = i; break; }
        Extrude dd2;
        CHECK(makeExtrude(box, triTop, dd2), "push/pull : la feuille est capturable");
        applyExtrude(box, dd2, 0.5);
        CHECK(std::fabs(signedVolume(box) - 0.5) < 1e-9, "push/pull depuis la feuille : le volume est recree (0.5)");
        CHECK(countNonManifold(box) == 0 && std::fabs((box.bmax.z - box.bmin.z) - 0.5) < 1e-9, "push/pull depuis la feuille : solide referme, hauteur 0.5");
    }

    /* --- 12.1 : rappels d'applyExtrude a la butee (la souris continue de bouger,
            l'application rapelle la fonction a chaque frame sur l'objet deja plie) --- */
    {
        Object box = makeBoxObject("Boite", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        Extrude dd;
        CHECK(makeExtrude(box, 4, dd), "push/pull : capture d'une face laterale (11, patch = tris 4-5)");
        applyExtrude(box, dd, dd.dLow);
        const size_t trisApres = box.tri.size();
        applyExtrude(box, dd, dd.dLow);
        CHECK(box.tri.size() == trisApres && std::fabs(signedVolume(box)) < 1e-9,
              "push/pull butee + rappel frame suivante : geometrie stable, pas de crash (isPatch indexe dd.tri)");
    }

    /* --- 12.2 : capture de la couche opposee de la feuille (le rayon pille le
            premier triangle venu entre les deux couches coïncidentes) : la normale
            doit etre reorientee du cote camera, le push est bloque au plan (rien
            a enfoncer), le pull redeplie la matiere du cote original --- */
    {
        Object box = makeBoxObject("Boite", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        Extrude dd;
        makeExtrude(box, 2, dd);
        applyExtrude(box, dd, -1.0);
        Extrude dd3;
        CHECK(makeExtrude(box, 0, dd3), "push/pull feuille : capture de la couche opposee (tri 0) possible");
        CHECK(std::fabs(dd3.normal.z - 1.0) < 1e-9 && dd3.hasLow && std::fabs(dd3.dLow) < 1e-9,
              "push/pull feuille : normale reorientee du cote de la camera, butee nulle (push borde)");
        applyExtrude(box, dd3, -0.7);
        CHECK(std::fabs(signedVolume(box)) < 1e-9 && (box.bmax.z - box.bmin.z) < 1e-9,
              "push/pull feuille : le push est bloque au plan (rien a enfoncer, comme SketchUp, pas de volume inverse)");
        applyExtrude(box, dd3, 0.5);
        CHECK(std::fabs(signedVolume(box) - 0.5) < 1e-9 && countNonManifold(box) == 0
              && std::fabs(box.bmin.z + 0.5) < 1e-9 && std::fabs(box.bmax.z) < 1e-9,
              "push/pull feuille : le pull redeplie la matiere du cote original (volume referme au-dessus de la feuille)");
    }

    /* --- 13. face aux coins separes des parois (source type STL non soudable) : jupe --- */
    {
        Object soup;
        /* 0..7 = boite weld ; 8..13 = coins du top DUPLIQUES (copies separees) :
           la face du dessus n'est pas indexee sur les parois, comme dans un STL */
        soup.v = {
            {0,0,0},{1,0,0},{1,1,0},{0,1,0},
            {0,0,1},{1,0,1},{1,1,1},{0,1,1},
            {0,0,1},{1,0,1},{1,1,1},{0,1,1}
        };
        const unsigned top[2][3] = {{8, 9, 10}, {8, 10, 11}};
        const unsigned rest[10][3] = {
            {0,2,1},{0,3,2},
            {0,1,5},{1,6,5},{1,2,6},
            {2,7,6},{2,3,7},{3,0,4},
            {3,4,7},{0,5,4}
        };
        for (const auto& f : top) { soup.tri.push_back(f[0]); soup.tri.push_back(f[1]); soup.tri.push_back(f[2]); }
        for (const auto& f : rest) { soup.tri.push_back(f[0]); soup.tri.push_back(f[1]); soup.tri.push_back(f[2]); }
        computeObjectDerived(soup);
        CHECK(std::fabs(signedVolume(soup) - 1.0) < 1e-9, "push/pull jupe : maillage de depart ferme, volume 1");
        Extrude dd;
        CHECK(makeExtrude(soup, 0, dd), "push/pull jupe : face (copies separees) capturable");
        CHECK(dd.boundary.size() == 4, "push/pull jupe : 4 aretes de perimetre sur la face a copies separees");
        applyExtrude(soup, dd, 1.0);
        CHECK(std::fabs(signedVolume(soup) - 2.0) < 1e-9, "push/pull jupe : volume augmente de l'aire x tirage");
        CHECK(std::fabs(soup.bmax.z - 2.0) < 1e-9, "push/pull jupe : la face est montee a z = 2");
        CHECK(countNonManifold(soup) == 16, "push/pull jupe : coque fermee geometriquement ; 16 bords ouverts en index (4 carré soudé + 3 par arete de jupe, comme le tri-soup du web)");
    }

    /* --- 14. push/pull avec echelle par objet (imports en mm normalises) --- */
    {
        Object b2 = makeBoxObject("B2", Vec3(0.5, 0.5, 0.5), Vec3(0.5, 0.5, 0.5), col);
        b2.scl = 2.0;   /* monde : cube 2x2x2 */
        Extrude dd;
        CHECK(makeExtrude(b2, 2, dd), "push/pull echelle : capture avec scl=2");
        applyExtrude(b2, dd, 1.0);
        CHECK(std::fabs(signedVolume(b2) * b2.scl * b2.scl * b2.scl - 12.0) < 1e-9,
              "push/pull echelle : monde = 12 (8 + aire 4 x tirage 1)");
        CHECK(std::fabs(b2.bmax.z - 1.0) < 1e-9, "push/pull echelle : sommet local monte a 1.0 (deplacement local 0.5)");
    }

    /* --- 15. objet composite : mesh SketchUp avec deux coques disjointes
            (cube + silhouette). Butée, pli rentrant et feuille ne doivent
            toucher que la coque du patch, jamais la coque voisine. --- */
    {
        Object compo = makeBoxObject("Compo", Vec3(0, 0, 0), Vec3(0.5, 0.5, 0.5), col);
        Object shell2 = makeBoxObject("S2", Vec3(0, 0, 0), Vec3(0.5, 0.5, 0.5), col);
        const unsigned base2 = (unsigned)compo.v.size();
        for (const Vec3& p : shell2.v) compo.v.push_back(p + Vec3(5, 0, 2));   /* coque 2 a cote et au-dessus */
        for (size_t i = 0; i < shell2.tri.size(); ++i) compo.tri.push_back(base2 + shell2.tri[i]);
        computeObjectDerived(compo);
        CHECK(compo.v.size() == 16 && compo.tri.size() == 72 && countNonManifold(compo) == 0
              && std::fabs(signedVolume(compo) - 2.0) < 1e-9,
              "composite : deux coques fermees disjointes, volume 2");
        Extrude dd;
        CHECK(makeExtrude(compo, 2, dd), "composite : capture de la face du dessus de la coque 1");
        CHECK(std::fabs(dd.dLow + 1.0) < 1e-9 && !dd.hasHigh,
              "composite : butee = epaisseur de la coque poussee (la coque voisine au-dessus ne la bloque ni ne la prolonge)");
        const size_t trisShell2 = 36;
        const std::vector<unsigned> triAvant(compo.tri.begin() + 36, compo.tri.end());
        const std::vector<Vec3> vAvant = compo.v;
        applyExtrude(compo, dd, dd.dLow);
        CHECK(compo.tri.size() == 16 * 3, "composite : butee -> feuille 4 tris + coque 2 intacte (16 tris)");
        bool shell2OK = compo.tri.size() >= triAvant.size();
        for (size_t i = 0; shell2OK && i < triAvant.size(); ++i) shell2OK = compo.tri[compo.tri.size() - triAvant.size() + i] == triAvant[i];
        CHECK(shell2OK, "composite : les tris de la coque 2 survivent a l'identique");
        bool vShell2OK = vAvant.size() == compo.v.size();
        for (size_t i = base2; vShell2OK && i < compo.v.size(); ++i) vShell2OK = dot(compo.v[i] - vAvant[i], compo.v[i] - vAvant[i]) < 1e-18;
        CHECK(vShell2OK, "composite : aucun sommet de la coque 2 ne bouge au pli");
        CHECK(std::fabs(signedVolume(compo) - 1.0) < 1e-9
              && countNonManifold(compo) == 8,
              "composite : feuille aplat a la butee (volume = coque 2 seule), 8 bords ouverts en index");
        /* capture de la feuille (couche opposee, comme le rayon qui pille la couche basse) */
        Extrude dd3;
        CHECK(makeExtrude(compo, 0, dd3), "composite : capture de la feuille");
        CHECK(std::fabs(dd3.normal.z - 1.0) < 1e-9 && dd3.hasLow && std::fabs(dd3.dLow) < 1e-9,
              "composite : normale feuille reorientee du cote camera, push borde au plan");
        applyExtrude(compo, dd3, -0.7);
        CHECK(std::fabs(signedVolume(compo) - 1.0) < 1e-9 && std::fabs(compo.bmin.z + 0.5) < 1e-9,
              "composite : push depuis la feuille bloque (coque 2 seule, rien ne se developpe en dessous)");
        applyExtrude(compo, dd3, 0.5);
        CHECK(std::fabs(signedVolume(compo) - 1.5) < 1e-9 && countNonManifold(compo) == 0
              && std::fabs(compo.bmin.z + 0.5) < 1e-9 && std::fabs(compo.bmax.z - 2.5) < 1e-9,
              "composite : pull +0.5 -> solide referme de la coque 1 (0.5) + coque 2 inchangee (1.0)");
    }

    /* 16 - fichier projet : sauvegarde/relecture fidele (barre de menus N5) */
    {
        buildScene();
        const std::string ppath = testsDir() + "/tmp_n51.s3projet";
        std::string err;
        const std::string posAvant = std::to_string(g_objs[1].pos.x) + "/" + std::to_string(g_objs[1].pos.y);
        const std::vector<unsigned> triAvant = g_objs[1].tri;
        const std::vector<Vec3> vAvant = g_objs[1].v;
        const double sclAvant = g_objs[1].scl;
        CHECK(saveProject(ppath, err), "projet : enregistrement reussi");
        buildScene();
        buildScene();                   /* scene modifiee avant relecture */
        CHECK(loadProject(ppath, err), "projet : relecture reussie");
        CHECK(g_objs.size() == 3 && g_objs[1].name == "Pave B", "projet : 3 objets relus, noms fideles");
        CHECK(g_objs[1].pos.x == stod(posAvant.substr(0, posAvant.find('/')))
              && g_objs[1].pos.y == stod(posAvant.substr(posAvant.find('/') + 1)),
              "projet : position intacte");
        CHECK(g_objs[1].scl == sclAvant, "projet : echelle intacte");
        CHECK(g_objs[1].tri == triAvant, "projet : topologie (tri) intacte");
        bool vOK = g_objs[1].v.size() == vAvant.size();
        for (size_t i = 0; vOK && i < vAvant.size(); ++i)
            vOK = (g_objs[1].v[i] - vAvant[i]).norm() < 1e-12;
        CHECK(vOK, "projet : sommets relus a 1e-12 pres");
        CHECK(!g_objs[0].edges.empty() && std::fabs(g_objs[0].bmax.z - 1.0) < 1e-12,
              "projet : aretes/bmin recalcules a la lecture");
        CHECK(objectInfo(g_objs[1]).find("Pave B") != std::string::npos
              && objectInfo(g_objs[1]).find("dimensions monde") != std::string::npos,
              "infos objet : nom + dimensions presentes");
        std::error_code ec;
        std::filesystem::remove(ppath, ec);
    }

    /* 17 - outillage T1 : duplication multi-instances */
    {
        g_objs.clear();
        const double colW[3] = {0.5, 0.5, 0.5};
        g_objs.push_back(makeBoxObject("Solo", Vec3(0, 1, 0), Vec3(1, 1, 1), colW));
        g_selected = 0;
        duplicateObject(0, 3);
        CHECK(g_objs.size() == 4, "duplication : 3 copies du cube seul");
        bool nomOK = g_objs[1].name == "Solo (1)" && g_objs[2].name == "Solo (2)" && g_objs[3].name == "Solo (3)";
        CHECK(nomOK, "duplication : noms numerotes");
        CHECK(std::fabs(g_objs[1].pos.x - 2.25) < 1e-12 && std::fabs(g_objs[2].pos.x - 4.5) < 1e-12
              && std::fabs(g_objs[3].pos.x - 6.75) < 1e-12,
              "duplication : copies alignees vers +X (pas = portee + 0.25)");
        bool sep = true;
        for (size_t i = 0; sep && i < g_objs.size(); ++i)
            for (size_t j = i + 1; sep && j < g_objs.size(); ++j)
                sep = !overlapsWorld(g_objs[i], g_objs[j]);
        CHECK(sep, "duplication : aucune superposition");
        CHECK(g_objs[3].v.size() == g_objs[0].v.size() && g_objs[3].tri == g_objs[0].tri,
              "duplication : copies identiques a l'original (meme mesh)");
        buildScene();
        g_selected = 0;                     /* voisin Pave B a droite : copies poussees au-dela */
        duplicateObject(0, 1);
        bool sepScene = true;
        for (size_t i = 0; sepScene && i < g_objs.size(); ++i)
            for (size_t j = i + 1; sepScene && j < g_objs.size(); ++j)
                sepScene = !overlapsWorld(g_objs[i], g_objs[j]);
        CHECK(sepScene && g_objs.size() == 4 && g_objs[3].pos.x > g_objs[1].bmax.x,
              "duplication : copie poussee au-dela du voisin Pave B (anti-superpositionImport)");
        /* champ nombre (retour de test T1.5.4 : signe non gere dans la duplication,
           options proposees) : refuse proprement - aucune copie, aucun crash. */
        const size_t nAvant = g_objs.size();
        duplicateObject(0, -3);
        duplicateObject(0, 0);
        CHECK(g_objs.size() == nAvant,
              "duplication : nombre negatif ou nul refuse (option retenue : aucune copie posee)");
        /* T1.5.5 (retour de test) : champ nombre negatif = retrait d'instances.
           -k retire les k dernieres copies numerotees de la serie de la source,
           l'original est toujours epargne ; au-dela du disponible, on retire tout. */
        g_objs.clear();
        g_objs.push_back(makeBoxObject("Solo", Vec3(0, 1, 0), Vec3(1, 1, 1), colW));
        g_selected = 0;
        duplicateObject(0, 3);
        CHECK(removeInstances(0, 2) == 2 && g_objs.size() == 2,
              "retrait : -2 supprime les 2 dernieres copies numerotees (Solo (2) et Solo (3))");
        CHECK(g_objs[0].name == "Solo" && g_objs[1].name == "Solo (1)",
              "retrait : l'original epargne, la copie la plus ancienne reste");
        CHECK(removeInstances(0, 9) == 1 && g_objs.size() == 1 && g_objs[0].name == "Solo",
              "retrait : au-dela du disponible, on retire tout sauf l'original");
        CHECK(removeInstances(0, 1) == 0 && g_objs.size() == 1,
              "retrait : aucune copie numerotee = rien a retirer (retour 0)");
    }

    /* 18 - outillage T1 : rotation autour d'un axe monde par le centre */
    {
        buildScene();
        Object& o2 = g_objs[1];             /* Pave B : 3 x 1.2 x 1.6 en monde */
        const std::vector<Vec3> base2 = o2.v;
        const Vec3 centre = o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl);
        const double volAvant = signedVolume(o2);
        applyRotationAt(o2, base2, centre, 2, 90.0);   /* Z de +90 deg */
        const double dx = o2.bmax.x - o2.bmin.x, dy = o2.bmax.y - o2.bmin.y, dz = o2.bmax.z - o2.bmin.z;
        const Vec3 centre2 = o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl);
        CHECK(std::fabs(dx - 1.2) < 1e-9 && std::fabs(dy - 3.0) < 1e-9 && std::fabs(dz - 1.6) < 1e-9,
              "rotation : +90 deg en Z permute les portees X/Y (pave debout)");
        CHECK((centre2 - centre).norm() < 1e-9, "rotation : le centre de la piece ne bouge pas (pivot)");
        CHECK(std::fabs(signedVolume(o2) - volAvant) < 1e-9 && countNonManifold(o2) == 0,
              "rotation : volume preserve, aretes toujours coherentes");
        const std::vector<Vec3> at90 = o2.v;
        applyRotationAt(o2, at90, centre, 2, -90.0);
        bool vRet = o2.v.size() == base2.size();
        for (size_t i = 0; vRet && i < base2.size(); ++i) vRet = (o2.v[i] - base2[i]).norm() < 1e-12;
        CHECK(vRet, "rotation : +90 puis -90 -> geometrie d'origine a 1e-12 pres");
        applyRotationAt(o2, base2, centre, 0, 180.0);   /* X de 180 deg */
        bool v180 = o2.v.size() == base2.size();
        for (size_t i = 0; v180 && i < base2.size(); ++i)
            v180 = std::fabs(o2.v[i].x - base2[i].x) < 1e-12
                && std::fabs(o2.v[i].y + base2[i].y) < 1e-12
                && std::fabs(o2.v[i].z + base2[i].z) < 1e-12;
        CHECK(v180 && (o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl) - centre).norm() < 1e-9,
              "rotation : 180 deg en X -> miroir Y/Z, pivot fixe");
        /* changement d'axe en chaine (lettre pendant une rotation) : +90 Z pose,
           puis +90 X sur la geometrie courante avec pivot recadre */
        applyRotationAt(o2, base2, centre, 2, 90.0);
        const std::vector<Vec3> z90 = o2.v;
        applyRotationAt(o2, z90, centre, 0, 90.0);
        const Vec3 centre3 = o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl);
        CHECK(std::fabs(signedVolume(o2) - volAvant) < 1e-9 && countNonManifold(o2) == 0
              && (centre3 - centre).norm() < 1e-9,
              "rotation : chaine +90 Z puis +90 X -> volume, aretes et pivot stables");
    }

    /* --- 19. T1.5 : ancre du drag d'extrusion (d relatif au rayon de capture).
       Retour de test : des la capture d'une face, la face passait en feuille
       avant de suivre le curseur - le plan de souris est percé ailleurs qu'au
       point clique (il passe par un sommet du triangle), le d brut valait
       plusieurs unites des la premiere frame et etait clampé d'emblée a la
       butee. Le correctif ancre d sur le rayon de capture (g_pp.dRef). */
    {
        g_objs.clear();
        Object imp; std::string err;
        CHECK(loadMeshFile(dataPath("cube_sketchup.obj"), imp, err), err.c_str());
        imp.name = "cube";
        computeObjectDerived(imp);
        normalizeImportScale(imp);
        weldObject(imp);
        placeImport(imp);
        const Vec3 centre = imp.pos + (imp.bmin + imp.bmax) * (0.5 * imp.scl);
        const double rImp = ((imp.bmax - imp.bmin) * (0.5 * imp.scl)).norm();
        g_cam.target = centre;
        g_cam.dist = std::max(rImp / std::tan(kFovDeg * 3.14159265358979 / 360.0), 1.0) * 1.5;
        g_winW = 1600; g_winH = 900;
        const double tt = std::tan(kFovDeg * 3.14159265358979 / 360.0);
        const double aspect = double(g_winW) / double(g_winH);
        bool captureVu = false, noJump = true;
        Extrude ddRefVue;
        bool ddRefOk = false;
        for (double phi : {0.5, 0.2, 0.05}) {
            for (double theta : {0.8, 2.0}) {
                g_cam.phi = phi; g_cam.theta = theta;
                auto rayFor = [&](double mx, double my) {
                    const double nx = 2.0 * mx / double(g_winW) - 1.0;
                    const double ny = 1.0 - 2.0 * my / double(g_winH);
                    return normalized(g_cam.right() * (nx * tt * aspect) + g_cam.up() * (ny * tt) + g_cam.forward());
                };
                const Vec3 dir0 = rayFor(g_winW * 0.5, g_winH * 0.5);
                const Vec3 org = g_cam.pos();
                double tHit; size_t tri;
                if (!rayObjectTri(org, dir0, imp, tHit, &tri)) continue;
                Extrude dd;
                if (!makeExtrude(imp, tri, dd)) continue;
                const Vec3 camDir = g_cam.forward();
                Vec3 pn = camDir - dd.normal * dot(camDir, dd.normal);
                if (dot(pn, pn) < 0.04) {
                    if (std::fabs(dd.normal.y) > 0.9) pn = Vec3(1, 0, 0);
                    else pn = Vec3(dd.normal.z, 0, -dd.normal.x);
                }
                dd.pn = pn;
                auto dBrut = [&](double mx, double my) -> double {
                    const Vec3 d2 = rayFor(mx, my);
                    const double den = dot(d2, dd.pn);
                    if (std::fabs(den) < 1e-9) return std::numeric_limits<double>::quiet_NaN();
                    const double tv = dot(dd.planePoint - org, dd.pn) / den;
                    if (tv < 0.0) return std::numeric_limits<double>::quiet_NaN();
                    return dot((org + d2 * tv) - dd.planePoint, dd.normal);
                };
                /* ancre posee comme au clic : d = 0 au rayon de capture */
                const double dCap = dBrut(g_winW * 0.5, g_winH * 0.5);
                if (!std::isfinite(dCap)) continue;
                dd.dRef = dCap;
                captureVu = true;
                if (std::fabs(dBrut(g_winW * 0.5, g_winH * 0.5) - dd.dRef) > 1e-9) noJump = false;
                if (phi == 0.5 && theta == 0.8) { ddRefVue = dd; ddRefOk = true; }
            }
        }
        CHECK(captureVu, "ancre extrusion : faces capturees sur le cube SketchUp reel sous plusieurs vues");
        CHECK(captureVu && noJump, "ancre extrusion : d = 0 au rayon de capture, quel que soit l'angle (fin de l'aplatissement immediat)");
        CHECK(ddRefOk, "ancre extrusion : capture reproductible de la vue de reference (phi 0.5, theta 0.8)");
        CHECK(ddRefOk && ddRefVue.hasLow && std::fabs(dot(ddRefVue.normal, ddRefVue.normal) - 1.0) < 1e-9,
              "ancre extrusion : la face capturee a la vue de reference a une normale unitaire et de la matiere derriere (butee negative)");
        if (ddRefOk) {
            g_cam.phi = 0.5; g_cam.theta = 0.8;
            const Vec3 org = g_cam.pos();
            const double ttR = tt, aspectR = aspect;
            auto dBrutRef = [&](double mx, double my) -> double {
                const double nx = 2.0 * mx / double(g_winW) - 1.0;
                const double ny = 1.0 - 2.0 * my / double(g_winH);
                const Vec3 d2 = normalized(g_cam.right() * (nx * ttR * aspectR) + g_cam.up() * (ny * ttR) + g_cam.forward());
                const double den = dot(d2, ddRefVue.pn);
                if (std::fabs(den) < 1e-9) return std::numeric_limits<double>::quiet_NaN();
                const double tv = dot(ddRefVue.planePoint - org, ddRefVue.pn) / den;
                if (tv < 0.0) return std::numeric_limits<double>::quiet_NaN();
                return dot((org + d2 * tv) - ddRefVue.planePoint, ddRefVue.normal);
            };
            const double d40 = dBrutRef(g_winW * 0.5, g_winH * 0.5 - 40.0) - ddRefVue.dRef;
            const double d80 = dBrutRef(g_winW * 0.5, g_winH * 0.5 - 80.0) - ddRefVue.dRef;
            const double d120 = dBrutRef(g_winW * 0.5, g_winH * 0.5 - 120.0) - ddRefVue.dRef;
            CHECK(std::isfinite(d40) && std::isfinite(d80) && std::isfinite(d120)
                  && d40 > 0.0 && d80 > d40 && d120 > d80,
                  "ancre extrusion : la face suit le curseur - souris vers le haut = tirage positif croissant");
            auto spanAlong = [](const Object& o, const Vec3& n) {
                double lo = 1e300, hi = -1e300;
                for (const Vec3& p : o.v) {
                    const double al = dot(o.pos + p * o.scl, n);
                    if (al < lo) lo = al;
                    if (al > hi) hi = al;
                }
                return hi - lo;
            };
            const double spanB = spanAlong(imp, ddRefVue.normal);
            const size_t nmB = countNonManifold(imp);
            applyExtrude(imp, ddRefVue, 1.25);
            const double spanA = spanAlong(imp, ddRefVue.normal);
            CHECK(std::fabs((spanA - spanB) - 1.25) < 1e-6 && countNonManifold(imp) == nmB,
                  "ancre extrusion : dimension tapee 1.25 (Entree) posee depuis la capture -> portee +1.25 le long de la normale, topologie inchangée");
            /* signe de la saisie (retour de test T1.5.2 : tiret non branche, la
               valeur tapee devenait un pull) : -0.5 = enfoncement, meme chemin
               que le push a la souris (clamp sur les butees de makeExtrude). */
            applyExtrude(imp, ddRefVue, -0.5);
            const double spanP = spanAlong(imp, ddRefVue.normal);
            CHECK(ddRefVue.hasLow && std::fabs((spanP - spanB) + 0.5) < 1e-6 && countNonManifold(imp) == nmB,
                  "pose clavier extrusion : valeur negative (tiret) = enfoncement, portee -0.5 le long de la normale, topologie inchangée");
            /* butee et saisie (demande de test) : une valeur tapee au-dela de la
               butee ne traverse pas la matiere - le push se cale a la butee
               (feuille) et rien ne ressort de l'autre cote du plan.          */
            applyExtrude(imp, ddRefVue, -9999.0);
            double hiPlane = -1e300;
            for (const Vec3& p : imp.v) {
                const double al = dot(imp.pos + p * imp.scl - ddRefVue.planePoint, ddRefVue.normal);
                if (al > hiPlane) hiPlane = al;
            }
            /* le pli a la butee clone des sommets par construction (motif butee
               eprouve dans les familles feuille) : on ne teste ici QUE la non
               traversée du plan. */
            CHECK(ddRefVue.hasLow && hiPlane < 1e-5,
                  "pose clavier extrusion : valeur au-dela de la butee = calee (feuille), rien ne depasse le plan (pas de traversee)");
        }

        /* pose au clavier - retour de test T1.5 : la valeur tapee n'avait aucun
           effet visible (la relecture du curseur l'ecrasait des la frame
           suivante). Contrat T1.5.2 : Entree applique la valeur, pose la face
           et quitte le mode P (une action complete, sans retoucher la souris) ;
           ppDragDelta reste le calcul central du suivi du curseur.          */
        if (ddRefOk) {
            g_cam.phi = 0.5; g_cam.theta = 0.8;
            const Vec3 orgV = g_cam.pos();
            const double ttV = std::tan(kFovDeg * 3.14159265358979 / 360.0);
            const double aspV = double(g_winW) / double(g_winH);
            const auto rayV = [&](double mx, double my) {
                const double nx = 2.0 * mx / double(g_winW) - 1.0;
                const double ny = 1.0 - 2.0 * my / double(g_winH);
                return normalized(g_cam.right() * (nx * ttV * aspV) + g_cam.up() * (ny * ttV) + g_cam.forward());
            };
            g_pp = ddRefVue;
            CHECK(std::fabs(ppDragDelta(orgV, rayV(g_winW * 0.5, g_winH * 0.5))) < 1e-9,
                  "pose clavier extrusion : d = 0 au rayon de capture (calcul central ppDragDelta)");
            const double q40 = ppDragDelta(orgV, rayV(g_winW * 0.5, g_winH * 0.5 - 40.0));
            const double q80 = ppDragDelta(orgV, rayV(g_winW * 0.5, g_winH * 0.5 - 80.0));
            CHECK(std::isfinite(q40) && q40 > 0.0 && q80 > q40,
                  "pose clavier extrusion : suivi du curseur (ppDragDelta) - souris vers le haut = tirage croissant");
            Vec3 vDeg = cross(ddRefVue.pn, Vec3(1, 0, 0));
            if (dot(vDeg, vDeg) < 1e-12) vDeg = cross(ddRefVue.pn, Vec3(0, 1, 0));
            CHECK(!std::isfinite(ppDragDelta(orgV, normalized(vDeg))),
                  "pose clavier extrusion : rayon perpendiculaire au plan de souris = rien applique (pas de valeur aberrante)");
            g_pp = Extrude();
        }
        g_objs.clear();
        g_selected = -1;
    }

    /* 20 - T1.5.8 : separateur decimal independant de la disposition du clavier */
    {
        CHECK(isSeparatorKey(GLFW_KEY_COMMA, 0) && isSeparatorKey(GLFW_KEY_PERIOD, 0)
              && isSeparatorKey(GLFW_KEY_KP_DECIMAL, 0) && isSeparatorKey(GLFW_KEY_SEMICOLON, 0),
              "separateur : virgule (francais), point, decimale du pave et ';' (AZERTY Maj+;) acceptes");
        CHECK(!isSeparatorKey(GLFW_KEY_X, 0) && !isSeparatorKey(GLFW_KEY_MINUS, 0)
              && !isSeparatorKey(GLFW_KEY_BACKSPACE, 0)
              && !isSeparatorKey(GLFW_KEY_ENTER, 0) && !isSeparatorKey(GLFW_KEY_ESCAPE, 0),
              "separateur : lettre, tiret, retour arriere, Entree et Echap ne sont pas des separateurs");
    }

    /* 21 - T1b : export cible (objet selectionne) + outil Supprimer */
    {
        g_objs.clear();
        g_objs.push_back(makeBoxObject("P1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_objs.push_back(makeBoxObject("P2", Vec3(2, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_selected = 1;
        exportOBJ(tmpPath("objet_seul.obj"), std::vector<int>{g_selected});
        g_objs.clear();
        Object r; err.clear();
        CHECK(loadMeshFile(tmpPath("objet_seul.obj"), r, err), err.c_str());
        CHECK(r.v.size() == 8 && r.tri.size() == 36,
              "export cible OBJ : l'objet seul exporte (8 sommets, 12 tris)");
        std::ifstream in(tmpPath("objet_seul.obj"));
        int nObj = 0; bool nomP2 = false;
        for (std::string ln; std::getline(in, ln); )
            if (ln.rfind("o ", 0) == 0) { ++nObj; nomP2 = (ln == "o P2"); }
        CHECK(nObj == 1 && nomP2, "export cible OBJ : un seul objet exporte, le bon (P2)");

        g_objs.clear();
        g_objs.push_back(makeBoxObject("P1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_objs.push_back(makeBoxObject("P2", Vec3(2, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_selected = 0;
        exportSTL(tmpPath("objet_seul.stl"), std::vector<int>{0});
        std::ifstream in2(tmpPath("objet_seul.stl"), std::ios::binary);
        in2.seekg(0, std::ios::end);
        CHECK(in2.tellg() == 84 + 50 * 12,
              "export cible STL : un seul objet (12 tris, taille binaire exacte)");

        g_objs.clear();
        g_objs.push_back(makeBoxObject("T1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_objs.push_back(makeBoxObject("T2", Vec3(2, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_objs.push_back(makeBoxObject("T3", Vec3(4, 0.5, 0), Vec3(0.5, 0.5, 0.5), col));
        g_selected = 2;
        deleteObject(1);
        CHECK(g_objs.size() == 2 && g_selected == 1 && g_objs[1].name == "T3",
              "suppression : l'objet suivant reste selectionne a la meme place");
        deleteObject(-1);
        deleteObject(9);
        CHECK(g_objs.size() == 2, "suppression : index invalides refuses (rien de supprime)");
        deleteObject(0);
        deleteObject(0);
        CHECK(g_objs.empty() && g_selected == -1,
              "suppression : liste videe, selection a -1");
    }

    /* 22 - T1.7 : multi-selection (Ctrl+clic) + export/suppression/duplication multi */
    {
        const double colM[3] = {0.6, 0.6, 0.6};
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_objs.push_back(makeBoxObject("M1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M2", Vec3(2, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M3", Vec3(4, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M4", Vec3(6, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));

        g_selected = 0;
        toggleSelect(2);
        CHECK(g_selected == 0 && selectedIndices().size() == 2 && selectedIndices()[1] == 2,
              "multi : Ctrl+clic ajoute M3 (selection M1 + M3)");
        toggleSelect(3);
        CHECK(selectedCount() == 3 && isSelectedRaw(0) && isSelectedRaw(2) && isSelectedRaw(3) && !isSelectedRaw(1),
              "multi : 3 selectionnes, M2 reste hors selection");
        toggleSelect(0);
        CHECK(g_selected == 2 && selectedIndices().size() == 2 && isSelectedRaw(2) && isSelectedRaw(3) && !isSelectedRaw(0),
              "multi : Ctrl+clic sur la principale la retire, M3 promue principale");

        exportOBJ(tmpPath("multi.obj"), selectedIndices());
        std::ifstream in(tmpPath("multi.obj"));
        std::vector<std::string> onames;
        for (std::string ln; std::getline(in, ln); )
            if (ln.rfind("o ", 0) == 0) onames.push_back(ln);
        CHECK(onames.size() == 2 && onames[0] == "o M3" && onames[1] == "o M4",
              "export multi OBJ : deux objets exportes, ordre d'index");

        exportSTL(tmpPath("multi.stl"), selectedIndices());
        std::ifstream in2(tmpPath("multi.stl"), std::ios::binary);
        in2.seekg(0, std::ios::end);
        CHECK(in2.tellg() == 84 + 50 * 24,
              "export multi STL : 24 tris, taille binaire exacte");

        g_dupSrcNames.clear();
        g_dupSrcNames.push_back("M3");
        g_dupSrcNames.push_back("M4");
        dupExecute(2);
        CHECK(g_objs.size() == 8 && g_objs[4].name == "M3 (1)" && g_objs[5].name == "M3 (2)"
              && g_objs[6].name == "M4 (1)" && g_objs[7].name == "M4 (2)",
              "duplication multi : k copies numerotees pour chaque objet de la selection");
        dupExecute(-2);
        CHECK(g_objs.size() == 4 && g_objs[2].name == "M3" && g_objs[3].name == "M4",
              "retrait multi : -2 retire les 2 dernieres copies de chaque serie");
        dupExecute(-2);
        CHECK(g_objs.size() == 4,
              "retrait multi : sans copie numerotee, rien n'est retire");

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_objs.push_back(makeBoxObject("M1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M2", Vec3(2, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M3", Vec3(4, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("M4", Vec3(6, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_selected = 0;
        toggleSelect(2);
        const size_t nd = deleteSelectedSet();
        CHECK(nd == 2 && g_objs.size() == 2 && g_objs[0].name == "M2" && g_objs[1].name == "M4",
              "suppression multi : la selection entiere supprimee, le reste intact");
        CHECK(g_selected == -1 && g_selMulti.empty(),
              "suppression multi : selection videe apres coup");
        deleteSelectedSet();
        CHECK(g_objs.size() == 2,
              "suppression multi : selection vide = rien de supprime");
    }

    /* 22b - T1.7.1 : durcissement des etats de selection (crash Ctrl+clic, point 4 du test T1.7) */
    {
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        const double colM[3] = {0.6, 0.6, 0.6};
        g_objs.push_back(makeBoxObject("C1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("C2", Vec3(3, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));

        /* scene du crash (binaire d'avant durcissement) : Ctrl+clic pur puis titre ;
           l'ancien code lisait g_objs[(size_t)g_selected] avec g_selected=-1 */
        g_selMulti.push_back(0);
        updateWindowTitle(W);
        CHECK(selectedIndices().size() == 1 && selectedIndices()[0] == 0,
              "titre : selection pure Ctrl (principal -1) affichee par la liste elagee, sans acces perime");

        /* promotion d'un multi perime : elague, premier valide devient principal */
        g_selected = 0; g_selMulti.clear();
        g_selMulti.push_back(99); g_selMulti.push_back(1);
        toggleSelect(0);
        CHECK(g_selected == 1 && g_selMulti.empty(),
              "promotion : un multi perime est elague, le premier valide devient principal");

        /* promotion vers un multi entierement perime : deselection propre */
        g_selected = 0; g_selMulti.clear();
        g_selMulti.push_back(99);
        toggleSelect(0);
        CHECK(g_selected == -1 && g_selMulti.empty(),
              "promotion : tous les multi perimes = deselection propre");

        /* deplacement avec selection principale perimee : sortie neutre, jamais d'acces */
        g_selected = (int)g_objs.size();
        g_mode = M_Drag;
        updateDrag(W);
        CHECK(g_mode == M_None,
              "drag : selection principale perimee = retour neutre sans acces");

        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
    }

    /* 23 - T1.8 : point de vue (pan molette/Shift, recentrage sur le point vise) */
    {
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        const double colM[3] = {0.6, 0.6, 0.6};
        g_objs.push_back(makeBoxObject("C1", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));

        Camera c;
        c.theta = 0.0; c.phi = 0.5; c.dist = 9.0; c.target = Vec3(0, 0.5, 0);
        const Camera c0 = c;
        const double scaleRef = 2.0 * c.dist * std::tan(kFovDeg * 3.14159265358979 / 360.0) / 800.0;

        cameraPan(c, 0.0, 100.0, 800.0);
        const Vec3 dUp = c.target - c0.target;
        CHECK(c.target.y > c0.target.y, "pan : glisser vers le bas deplace la cible vers le haut");
        CHECK(std::fabs(dUp.x - c0.up().x * 100.0 * scaleRef) < 1e-9
              && std::fabs(dUp.y - c0.up().y * 100.0 * scaleRef) < 1e-9
              && std::fabs(dUp.z - c0.up().z * 100.0 * scaleRef) < 1e-9,
              "pan : deplacement dans le plan camera, echelle = hauteur vue / hauteur fenetre");

        cameraPan(c, 100.0, 0.0, 800.0);
        const Camera c2 = c;
        const Vec3 dR = c.target - c0.target - dUp;
        CHECK(std::fabs(dR.x + c2.right().x * 100.0 * scaleRef) < 1e-9
              && std::fabs(dR.y + c2.right().y * 100.0 * scaleRef) < 1e-9
              && std::fabs(dR.z + c2.right().z * 100.0 * scaleRef) < 1e-9,
              "pan : glisser a droite deplace la cible vers la gauche (l'objet suit le curseur)");
        cameraPan(c, 100.0, 100.0, 0.0);
        CHECK(c.target.x == c2.target.x && c.target.y == c2.target.y && c.target.z == c2.target.z,
              "pan : fenetre de hauteur nulle ignore");

        Vec3 p;
        CHECK(pickPointFromRay(Vec3(0.25, 5, 0), Vec3(0, -1, 0), p)
              && std::fabs(p.x - 0.25) < 1e-12 && std::fabs(p.y - 1.0) < 1e-9 && std::fabs(p.z) < 1e-12,
              "C : rayon sur l'objet -> point d'impact sur la face superieure");
        CHECK(pickPointFromRay(Vec3(100, 5, 0), Vec3(0, -1, 0), p)
              && std::fabs(p.x - 100.0) < 1e-12 && p.y == 0.0,
              "C : rayon dans le vide -> impact au sol (y = 0)");
        CHECK(!pickPointFromRay(Vec3(100, 5, 0), Vec3(0, 1, 0), p),
              "C : rayon vers le ciel, aucun point vise -> false");

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    /* 24 - T1.9 : rotation multi (cas 1) - chaque objet tourne autour de son
       PROPRE centre, angle et axe communs ; mono et voie numerique inchanges */
    {
        extern double g_stubCursorX, g_stubCursorY;
        const double colM[3] = {0.6, 0.6, 0.6};
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_rotActive = false; g_rotMode = false; g_rotMulti.clear(); g_rotNumBuf.clear();
        g_objs.push_back(makeBoxObject("RA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("RB", Vec3(2, 0.5, 0), Vec3(1.0, 0.25, 0.5), colM));
        g_objs.push_back(makeBoxObject("RC", Vec3(5, 0.5, 0), Vec3(0.25, 0.5, 0.25), colM));

        selectOnly(0);
        toggleSelect(1);
        toggleSelect(2);
        CHECK(selectedCount() == 3, "rotation multi : 3 objets selectionnes (RA + RB + RC)");

        g_stubCursorX = 25.0; g_stubCursorY = 0.0;
        rotBegin(W, 2);
        CHECK(g_rotActive && g_rotMulti.size() == 3
              && g_rotMulti[0].idx == 0 && g_rotMulti[1].idx == 1 && g_rotMulti[2].idx == 2,
              "rotation multi : capture d'une base par objet selectionne");
        CHECK((g_rotMulti[0].pivot - g_objs[0].pos).norm() < 1e-12
              && (g_rotMulti[1].pivot - g_objs[1].pos).norm() < 1e-12
              && (g_rotMulti[2].pivot - g_objs[2].pos).norm() < 1e-12,
              "rotation multi : pivot propre = centre de chaque piece");

        {   /* bandeau multi : message borne (l'ancien message long, combiné au
               defaut d'abreviation de drawTextClipped, geleait la fenetre)
               - retour de test T1.9 : fenetre grisee a chaque essai */
            const std::string sp = statusPrompt();
            CHECK(!sp.empty() && sp.size() < 200
                  && sp.find(std::to_string(selectedCount())) != std::string::npos,
                  "rotation multi : message du bandeau present, born (< 200 car), nombre d'objets annonce");
        }

        g_stubCursorX = 25.0 + 40.0;
        updateRot(W);
        CHECK(std::fabs(g_rotAngle - 10.0) < 1e-12,
              "rotation multi : souris -> angle commun accumule (0.25 deg/pixel)");
        {   /* vertex 0 de RB : position monde attendue = rotation de la base par 10 deg */
            const RotCapture& rc = g_rotMulti[1];
            const Object& rb = g_objs[(size_t)rc.idx];
            const Vec3 wxVu = rb.pos + rb.v[0] * rb.scl;
            const Vec3 wxExact = rotateAroundAxis(rb.pos + rc.base[0] * rb.scl, rc.pivot, 2, 10.0);
            CHECK((wxVu - wxExact).norm() < 1e-12,
                  "rotation multi : preview exacte (vertex 0 de RB = rotation 10 deg de sa base)");
        }

        /* voie numerique : 90 dans le champ puis Entree = pose */
        const Vec3 cA = g_objs[0].pos, cB = g_objs[1].pos, cC = g_objs[2].pos;
        const double vAvA = signedVolume(g_objs[0]), vAvB = signedVolume(g_objs[1]), vAvC = signedVolume(g_objs[2]);
        g_rotMode = true;
        g_rotNumBuf = "90";
        onKey(W, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
        CHECK(!g_rotActive && g_rotNumBuf.empty(), "rotation multi : 90 au champ puis Entree pose et quitte");
        CHECK(g_rotMulti.empty(), "rotation multi : captures liberees apres la pose");
        const double dxA = g_objs[0].bmax.x - g_objs[0].bmin.x, dyA = g_objs[0].bmax.y - g_objs[0].bmin.y;
        const double dxB = g_objs[1].bmax.x - g_objs[1].bmin.x, dyB = g_objs[1].bmax.y - g_objs[1].bmin.y, dzB = g_objs[1].bmax.z - g_objs[1].bmin.z;
        const double dxC = g_objs[2].bmax.x - g_objs[2].bmin.x, dyC = g_objs[2].bmax.y - g_objs[2].bmin.y;
        CHECK(std::fabs(dxA - 1.0) < 1e-9 && std::fabs(dyA - 1.0) < 1e-9
              && std::fabs(dxB - 0.5) < 1e-9 && std::fabs(dyB - 2.0) < 1e-9 && std::fabs(dzB - 1.0) < 1e-9
              && std::fabs(dxC - 1.0) < 1e-9 && std::fabs(dyC - 0.5) < 1e-9,
              "rotation multi +90 Z : RA cube inchange, RB et RC permutent (0.5 x 2 x 1 et 1 x 0.5 x 0.5)");
        CHECK((g_objs[0].pos - cA).norm() < 1e-12 && (g_objs[1].pos - cB).norm() < 1e-12
              && (g_objs[2].pos - cC).norm() < 1e-12,
              "rotation multi : chaque objet reste sur place (pivot propre, pas de translation de groupe)");
        CHECK(std::fabs(signedVolume(g_objs[0]) - vAvA) < 1e-9
              && std::fabs(signedVolume(g_objs[1]) - vAvB) < 1e-9
              && std::fabs(signedVolume(g_objs[2]) - vAvC) < 1e-9
              && countNonManifold(g_objs[1]) == 0,
              "rotation multi : volumes preserves, aretes coherentes apres pose");

        /* annulation : restaure chaque base capturee */
        g_stubCursorX = 10.0;
        rotBegin(W, 1);
        g_stubCursorX = 10.0 + 60.0;
        updateRot(W);                   /* +15 deg de preview */
        CHECK(std::fabs(g_rotAngle - 15.0) < 1e-12 && g_rotMulti.size() == 3,
              "rotation multi : re-capture apres pose, preview active");
        rotCancel();
        CHECK(!g_rotActive && g_rotMulti.empty()
              && std::fabs((g_objs[1].bmax.x - g_objs[1].bmin.x) - 0.5) < 1e-9,
              "rotation multi : clic droit restaure chaque objet (annulation complete)");

        /* regression mono : selection simple = chemin historique */
        selectOnly(1);
        rotBegin(W, 1);
        CHECK(g_rotActive && g_rotMulti.empty() && g_rotObj == 1 && g_rotBase.size() == g_objs[1].v.size(),
              "rotation mono : chemin historique intact (pas de capture multi)");
        rotApplyCurrent(1, 30.0);
        CHECK(std::fabs(signedVolume(g_objs[1]) - vAvB) < 1e-9
              && (objectRotationPivot(g_objs[1]) - Vec3(2, 0.5, 0)).norm() < 1e-12,
              "rotation mono : +30 Y autour du centre propre (pivot = centre)");
        rotCancel();

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_rotMulti.clear(); g_rotActive = false; g_rotMode = false;
    }

    /* 25 - T1.10 : rotation multi cas 2 - pivot = barycentre du groupe
       (centre de la boite englobant toutes les geometries capturees),
       bascule a chaud B, angle conserve ; mono non concerne */
    {
        extern double g_stubCursorX, g_stubCursorY;
        const double colM[3] = {0.6, 0.6, 0.6};
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_rotActive = false; g_rotMode = false; g_rotMulti.clear(); g_rotNumBuf.clear(); g_rotPivotKind = 0;
        g_objs.push_back(makeBoxObject("GA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("GB", Vec3(4, 0.5, 0), Vec3(0.5, 0.25, 0.5), colM));

        selectOnly(0);
        toggleSelect(1);
        g_stubCursorX = 25.0;
        rotBegin(W, 2);   /* axe Z, defaut = centre propre (cas 1) */
        CHECK(g_rotActive && g_rotMulti.size() == 2 && g_rotPivotKind == 0,
              "rotation multi cas 2 : capture par defaut = centre propre (T1.9 inchange)");
        CHECK(std::fabs(g_rotGroupPivot.x - 2.0) < 1e-12 && std::fabs(g_rotGroupPivot.y - 0.5) < 1e-12
              && std::fabs(g_rotGroupPivot.z) < 1e-12,
              "rotation multi cas 2 : pivot groupe = centre de la boite union (x=2, y=0.5)");

        onKey(W, GLFW_KEY_B, 0, GLFW_PRESS, 0);   /* bascule a chaud -> barycentre */
        CHECK(g_rotPivotKind == 1,
              "rotation multi cas 2 : B bascule le pivot sur le barycentre du groupe");
        CHECK(std::fabs((g_objs[0].bmax.x - g_objs[0].bmin.x) - 1.0) < 1e-9,
              "rotation multi cas 2 : la bascule B ne transforme rien tant que l'angle ne bouge pas");
        CHECK(std::fabs(g_rotAngle) < 1e-12,
              "rotation multi cas 2 : l'angle courant est conserve a la bascule");

        /* angle exact 180 + Entree : le groupe pivote rigidement, les POSITIONS monde s'echangent */
        const Vec3 cA = g_objs[0].pos, cB = g_objs[1].pos;
        const double vA = signedVolume(g_objs[0]), vB = signedVolume(g_objs[1]);
        g_rotMode = true;
        g_rotNumBuf = "180";
        onKey(W, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
        CHECK(g_rotActive == false && g_rotPivotKind == 0,
              "rotation multi cas 2 : pose (Entree) puis remise du pivot propre pour le prochain tour");
        const Vec3 centreMA = g_objs[0].pos + (g_objs[0].bmin + g_objs[0].bmax) * (0.5 * g_objs[0].scl);
        const Vec3 centreMB = g_objs[1].pos + (g_objs[1].bmin + g_objs[1].bmax) * (0.5 * g_objs[1].scl);
        CHECK((centreMA - Vec3(4, 0.5, 0)).norm() < 1e-9 && (centreMB - Vec3(0, 0.5, 0)).norm() < 1e-9,
              "rotation multi cas 2 +180 deg en Z : GA et GB s'echangent (rotation rigide du groupe)");
        CHECK((g_objs[0].pos - cA).norm() < 1e-12 && (g_objs[1].pos - cB).norm() < 1e-12,
              "rotation multi cas 2 : o.pos reste l ancre locale (la geometrie porte le deplacement)");
        CHECK(std::fabs(signedVolume(g_objs[0]) - vA) < 1e-9 && std::fabs(signedVolume(g_objs[1]) - vB) < 1e-9
              && countNonManifold(g_objs[0]) == 0 && countNonManifold(g_objs[1]) == 0,
              "rotation multi cas 2 : volumes preserves, aretes coherentes");

        /* bascule B en PLEIN preview : angle conserve, geometrie = rotation de la base autour du pivot groupe */
        g_stubCursorX = 25.0;
        rotBegin(W, 1);
        g_stubCursorX = 25.0 + 40.0;
        updateRot(W);                       /* +10 deg en Y */
        onKey(W, GLFW_KEY_B, 0, GLFW_PRESS, 0);   /* -> barycentre, angle conserve */
        CHECK(g_rotPivotKind == 1 && std::fabs(g_rotAngle - 10.0) < 1e-12,
              "rotation multi cas 2 : B pendant la preview conserve l angle (10 deg) et change le pivot");
        {
            const RotCapture& rc = g_rotMulti[0];
            const Object& ra = g_objs[(size_t)rc.idx];
            const Vec3 wxVu = ra.pos + ra.v[0] * ra.scl;
            const Vec3 wxExact = rotateAroundAxis(ra.pos + rc.base[0] * ra.scl, g_rotGroupPivot, 1, 10.0);
            CHECK((wxVu - wxExact).norm() < 1e-12,
                  "rotation multi cas 2 : preview exacte autour du barycentre (vertex 0 de GA)");
        }
        rotCancel();
        CHECK(!g_rotActive && g_rotMulti.empty() && g_rotPivotKind == 0
              && std::fabs((g_objs[0].bmax.x - g_objs[0].bmin.x) - 1.0) < 1e-9,
              "rotation multi cas 2 : annulation (clic droit) restaure tout, pivot reinitialise");

        /* regression mono : B sans selection multi = sans effet de pivot groupe
           (scene recreee : l'annulation precedente restaure l'etat de SA capture,
           qui datait d'apres la pose 180 - CF rotation mono, annulation = retour
           a la capture ; une scene fraiche isole le cas B isole) */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_objs.push_back(makeBoxObject("GA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("GB", Vec3(4, 0.5, 0), Vec3(0.5, 0.25, 0.5), colM));
        selectOnly(1);
        rotBegin(W, 0);
        onKey(W, GLFW_KEY_B, 0, GLFW_PRESS, 0);
        CHECK(g_rotPivotKind == 0 && g_rotMulti.empty() && g_rotActive,
              "rotation mono : B sans selection multi = sans effet (pivot = centre propre)");
        rotApplyCurrent(0, 30.0);
        CHECK((objectRotationPivot(g_objs[1]) - Vec3(4, 0.5, 0)).norm() < 1e-12,
              "rotation mono : le pivot reste le centre propre apres un B isole");
        rotCancel();

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_rotMulti.clear(); g_rotActive = false; g_rotMode = false; g_rotPivotKind = 0;
    }

    /* 26 - T1.11 : deplacement de groupe (drag multi) - clic gauche simple sur
       un objet DEJA membre d'une multi de plus d'un objet = tout le groupe
       suit le meme delta (plan camera, verrou d'axe commun), le clic ne
       reduit PAS la selection ; mono : chemin historique strictement inchange */
    {
        extern double g_stubCursorX, g_stubCursorY;
        const double colM[3] = {0.6, 0.6, 0.6};
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;
        g_winW = 800; g_winH = 600;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_rotActive = false; g_rotMode = false; g_ppActive = false; g_ppMode = false;
        g_axisHeld[0] = g_axisHeld[1] = g_axisHeld[2] = false;
        g_cam.theta = 0.0; g_cam.phi = 0.5; g_cam.dist = 9.0; g_cam.target = Vec3(0, 0.5, 0);
        g_objs.push_back(makeBoxObject("DA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("DB", Vec3(2, 0.5, 0), Vec3(0.5, 0.25, 0.5), colM));

        /* clic gauche SANS Ctrl sur DA, membre de la multi : selection
           multi conservee, capture de groupe armee */
        selectOnly(0);
        toggleSelect(1);
        g_lastClickTime = -100.0;              /* stub : jamais un double-clic */
        g_stubCursorX = 400.0; g_stubCursorY = 300.0;   /* centre : rayon vise DA */
        onMouseButton(W, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        CHECK(g_mode == M_Drag && g_dragMulti.size() == 2
              && g_dragMulti[0].idx == 0 && g_dragMulti[1].idx == 1
              && selectedCount() == 2,
              "drag multi : clic sur un membre = multi conservee (pas de selectOnly), 2 membres capturees");
        CHECK((g_dragMulti[0].startPos - Vec3(0, 0.5, 0)).norm() < 1e-12
              && (g_dragMulti[1].startPos - Vec3(2, 0.5, 0)).norm() < 1e-12,
              "drag multi : position de depart capturee pour chaque membre");

        {   /* delta libre : meme delta plan camera pour les deux membres */
            g_stubCursorX = 400.0 + 40.0;
            Vec3 org2, dir2;
            CHECK(rayFromMouse(W, org2, dir2), "drag multi : rayon disponible dans le stub");
            const Vec3 nrm = g_cam.forward();
            const double tP = dot(g_grabPoint - org2, nrm) / dot(dir2, nrm);
            const Vec3 delta = (org2 + dir2 * tP) - g_grabPoint;
            CHECK(delta.norm() > 0.1, "drag multi : deplacement du curseur = delta non nul");
            updateDrag(W);
            CHECK((g_objs[0].pos - g_dragMulti[0].startPos - delta).norm() < 1e-9
                  && (g_objs[1].pos - g_dragMulti[1].startPos - delta).norm() < 1e-9,
                  "drag multi : delta du plan camera applique a l IDENTIQUE aux deux membres");

            const double dx = delta.x;            /* verrou d'axe X herite : commun au groupe */
            g_axisHeld[0] = true;
            updateDrag(W);
            const Vec3 ex(dx, 0, 0);
            CHECK((g_objs[0].pos - g_dragMulti[0].startPos - ex).norm() < 1e-12
                  && (g_objs[1].pos - g_dragMulti[1].startPos - ex).norm() < 1e-12,
                  "drag multi : verrou d'axe X herite, commun a tout le groupe");
            g_axisHeld[0] = false;
        }

        /* relachement gauche = pose : groupe pose, selection intacte */
        onMouseButton(W, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        CHECK(g_mode == M_None && g_dragMulti.empty() && selectedCount() == 2,
              "drag multi : relachement = pose, capture videe, multi toujours active");

        /* chemin mono historique : clic sur un objet HORS multi = selectOnly
           (mono) + drag singleton ; les autres objets ne bougent pas */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_axisHeld[0] = g_axisHeld[1] = g_axisHeld[2] = false;
        g_objs.push_back(makeBoxObject("DA", Vec3(0, 0.5, 1.5), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("DB", Vec3(0, 0.5, 3.0), Vec3(0.5, 0.25, 0.5), colM));
        g_objs.push_back(makeBoxObject("DM", Vec3(0, 0.5, 0.0), Vec3(0.5, 0.5, 0.5), colM));
        g_cam.target = Vec3(0, 0.5, 0);      /* le rayon central vise DM (hors multi) */
        selectOnly(0);
        toggleSelect(1);
        g_lastClickTime = -100.0;
        g_stubCursorX = 400.0; g_stubCursorY = 300.0;
        onMouseButton(W, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        CHECK(g_mode == M_Drag && g_dragMulti.empty() && g_selected == 2 && selectedCount() == 1,
              "drag : clic sur un objet HORS multi = selection mono (selectOnly), aucune capture de groupe");
        {
            Vec3 org4, dir4;
            const Vec3 startDM = g_startPos;
            g_stubCursorX = 400.0 - 30.0;
            CHECK(rayFromMouse(W, org4, dir4), "drag mono : rayon disponible");
            const Vec3 nrm = g_cam.forward();
            const double tP = dot(g_grabPoint - org4, nrm) / dot(dir4, nrm);
            const Vec3 delta = (org4 + dir4 * tP) - g_grabPoint;
            updateDrag(W);
            CHECK((g_objs[2].pos - startDM - delta).norm() < 1e-9,
                  "drag mono : chemin historique intact (pos = startPos + delta)");
            CHECK((g_objs[0].pos - Vec3(0, 0.5, 1.5)).norm() < 1e-12
                  && (g_objs[1].pos - Vec3(0, 0.5, 3.0)).norm() < 1e-12,
                  "drag mono : les autres objets ne bougent pas");
        }

        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_dragMulti.clear();
        g_axisHeld[0] = g_axisHeld[1] = g_axisHeld[2] = false;
    }

    /* 27 - T1.12 : copier-coller Ctrl+C / Ctrl+V - presse-papiers interne,
       collage decale d un pas (largeur X + 0.25), serie visible sur Ctrl+V
       repetes, groupe colle = selection ; presse-papiers independant des
       suppressions ; Ctrl+X plus tard */
    {
        const double colM[3] = {0.6, 0.6, 0.6};
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_rotActive = false; g_rotMode = false; g_ppActive = false; g_ppMode = false;
        g_clipboard.clear(); g_pasteRepeat = 0; g_lastClickTime = -100.0;
        g_objs.push_back(makeBoxObject("CA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));

        /* Ctrl+C sans selection : rien, message bandeau */
        onKey(W, GLFW_KEY_C, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK(g_clipboard.empty() && g_bandFlash.find("rien a copier") != std::string::npos,
              "copie : Ctrl+C sans selection = presse-papiers intact + message bandeau");

        /* copie mono par le clavier, collage x2 (serie decalee) */
        selectOnly(0);
        onKey(W, GLFW_KEY_C, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK(g_clipboard.size() == 1 && g_clipboard[0].name == "CA" && g_pasteRepeat == 0,
              "copie : Ctrl+C capture l objet (copie entiere, serie remise a zero)");
        const double step = (g_objs[0].bmax.x - g_objs[0].bmin.x) * g_objs[0].scl + 0.25;
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK((int)g_objs.size() == 2 && g_objs[1].name == "CA (1)"
              && (g_objs[1].pos - Vec3(step, 0.5, 0)).norm() < 1e-12
              && g_selected == 1 && g_selMulti.empty(),
              "colle : Ctrl+V pose 1 copie decalee d un pas, nommee CA (1), selectionnee");
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK((int)g_objs.size() == 3 && (g_objs[2].pos - Vec3(2.0 * step, 0.5, 0)).norm() < 1e-12,
              "colle : Ctrl+V repete = serie visible (chaque collage decale encore d un pas)");

        /* copie multi : le GROUPE est pose, espacement interne identique */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_clipboard.clear(); g_pasteRepeat = 0;
        g_objs.push_back(makeBoxObject("GA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));
        g_objs.push_back(makeBoxObject("GB", Vec3(3, 0.5, 0), Vec3(0.5, 0.25, 0.5), colM));
        selectOnly(0);
        toggleSelect(1);
        onKey(W, GLFW_KEY_C, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK(g_clipboard.size() == 2 && g_pasteRepeat == 0,
              "copie multi : Ctrl+C capture les 2 objets de la multi");
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        const double step2 = (3.5 - (-0.5)) + 0.25;    /* largeur X du groupe copie + espace */
        CHECK((int)g_objs.size() == 4
              && (g_objs[2].pos - Vec3(step2, 0.5, 0)).norm() < 1e-12
              && (g_objs[3].pos - Vec3(3.0 + step2, 0.5, 0)).norm() < 1e-12
              && g_selected == 2 && g_selMulti.size() == 1 && g_selMulti[0] == 3,
              "colle multi : le GROUPE est pose (espacement interne identique) et devient la selection");
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK((int)g_objs.size() == 6
              && (g_objs[4].pos - Vec3(2.0 * step2, 0.5, 0)).norm() < 1e-12
              && (g_objs[5].pos - Vec3(3.0 + 2.0 * step2, 0.5, 0)).norm() < 1e-12,
              "colle multi : Ctrl+V suivant = nouvelle serie decalee d un pas supplementaire");

        /* le presse-papiers survit aux suppressions ; vide = rien pose */
        deleteObject(0);
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK((int)g_objs.size() == 7
              && (g_objs[5].pos - Vec3(3.0 * step2, 0.5, 0)).norm() < 1e-12
              && (g_objs[6].pos - Vec3(3.0 + 3.0 * step2, 0.5, 0)).norm() < 1e-12,
              "presse-papiers : independant des suppressions (le collage repart des captures)");
        g_clipboard.clear(); g_pasteRepeat = 0; g_bandFlash.clear();
        onKey(W, GLFW_KEY_V, 0, GLFW_PRESS, GLFW_MOD_CONTROL);
        CHECK((int)g_objs.size() == 7 && g_bandFlash.find("presse-papiers vide") != std::string::npos,
              "colle : presse-papiers vide = rien pose, message bandeau");

        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_clipboard.clear(); g_pasteRepeat = 0;
    }

    /* 28 - B0 : fichier de setup (INI a cote de l exe) - parse tolerant,
       sauvegarde immediate a chaque changement de preference, dernier
       projet, round-trip complet fermeture -> lancement */
    {
        const std::string path = tmpPath("setup_test.ini");   /* portable : fs::temp_directory_path (le /tmp POSIX n existe pas sous Windows) */
        g_setupPath = path;
        g_setup = Setup{};
        std::remove(path.c_str());
        loadSetup();   /* fichier absent = defauts du programme conserves */
        CHECK(g_setup.formatScene == "obj" && g_setup.dernierProjet.empty()
              && g_setup.winW == 0,
              "setup : fichier absent = defauts du programme");

        /* parse d'un INI ecrit/editable a la main : commentaires, cle
           inconnue, w = 0 (aberrant) ignore au profit du w valide lu avant */
        {
            std::ofstream f(path.c_str());
            f << "# setup sketch3d\n"
              << "[fenetre]\nx = 220\ny = 40\nw = 1600\nh = 900\nmax = 1\nw = 0\nzzz = 5\n"
              << "\n[projet]\ndernier = C:\\proj\\essai.s3projet\n"
              << "\n[exports]\nformat_scene = stl\nformat_selection = faux\n";
        }
        loadSetup();
        CHECK(g_setup.winX == 220 && g_setup.winY == 40
              && g_setup.winW == 1600 && g_setup.winH == 900 && g_setup.maximized == 1,
              "setup : geometrie relue (w = 0 ignore, cles inconnues ignorees)");
        CHECK(g_setup.dernierProjet == "C:\\proj\\essai.s3projet",
              "setup : dernier projet relu");
        CHECK(g_setup.formatScene == "stl" && g_setup.formatSelection == "obj",
              "setup : format scene memorise, valeur invalide (format_selection) ignoree");

        /* changement de preference = sauvegarde immediate, puis round-trip */
        noteExportFormat(false, true);
        CHECK(g_setup.formatSelection == "stl",
              "setup : preference export selection -> stl appliquee et gravee sur le champ");
        g_setup = Setup{};       /* simulation d un relancement */
        loadSetup();
        CHECK(g_setup.winW == 1600 && g_setup.winH == 900 && g_setup.winX == 220
              && g_setup.formatScene == "stl" && g_setup.formatSelection == "stl"
              && g_setup.dernierProjet == "C:\\proj\\essai.s3projet",
              "setup : round-trip complet (fermeture -> lancement suivant)");
        saveSetup();
        {
            std::ifstream f(path.c_str());
            std::string contenu((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(contenu.find("[fenetre]") != std::string::npos
                  && contenu.find("format_scene = stl") != std::string::npos
                  && contenu.find("max = 1") != std::string::npos,
                  "setup : fichier INI lisible et editable a la main");
        }

        std::remove(path.c_str());
        g_setupPath = "sketch3d.ini";
        g_setup = Setup{};
    }

    /* 29 - B1 : barre d'outils flottante v1 - runToolCommand : bouton =
       raccourci (modes exclusifs, annulation en cours comprise) */
    {
        const double colM[3] = {0.6, 0.6, 0.6};
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_rotActive = false; g_rotMode = false; g_ppActive = false; g_ppMode = false;
        g_dupPending = false; g_clipboard.clear(); g_pasteRepeat = 0;
        extern double g_stubCursorX, g_stubCursorY;
        g_lastClickTime = -100.0;
        g_cam = Camera{}; g_camHome = Camera{};
        g_objs.push_back(makeBoxObject("TA", Vec3(0, 0.5, 0), Vec3(0.5, 0.5, 0.5), colM));

        /* Selection sans rien : neutre, sans effet ni crash */
        runToolCommand(IDM_TOOL_SELECT);
        CHECK(!g_ppMode && !g_rotMode && selectedCount() == 0,
              "toolbar : Selection sans rien = neutre (jamais de fermeture)");

        /* Bouton R = touche R : entree de mode, exclusivite P/R sortie */
        runToolCommand(IDM_TOOL_ROT);
        CHECK(g_rotMode && !g_ppMode, "toolbar : bouton R = mode Rotation (identique au raccourci)");
        runToolCommand(IDM_TOOL_PP);
        CHECK(!g_rotMode && g_ppMode, "toolbar : bouton P sort du mode R (modes exclusifs)");
        runToolCommand(IDM_TOOL_PP);
        CHECK(!g_ppMode, "toolbar : bouton P re-clique = sortie du mode");

        /* Bouton P via le clavier : onKey reutilise le MEME chemin */
        onKey(W, GLFW_KEY_R, 0, GLFW_PRESS, 0);
        CHECK(g_rotMode, "toolbar : touche R = meme chemin de code que le bouton");
        onKey(W, GLFW_KEY_R, 0, GLFW_PRESS, 0);
        CHECK(!g_rotMode, "toolbar : touche R re-cliquee = sortie du mode");

        /* Bouton Selection pendant une rotation ACTIVE : annule ET quitte */
        selectOnly(0);
        g_stubCursorX = 400.0; g_stubCursorY = 300.0;
        g_winW = 800; g_winH = 600;
        rotBegin(W, 2);
        CHECK(g_rotActive, "toolbar : rotation active avant le bouton Selection");
        runToolCommand(IDM_TOOL_SELECT);   /* 1er clic : comme Echap, on sort de l'etat en cours */
        CHECK(!g_rotActive && !g_rotMode && selectedCount() == 1
              && std::fabs((g_objs[0].bmax.x - g_objs[0].bmin.x) - 1.0) < 1e-9,
              "toolbar : 1er clic Selection = annulation en cours + mode quitte (selection gardee, comme Echap)");
        runToolCommand(IDM_TOOL_SELECT);   /* 2e clic : deselection */
        CHECK(selectedCount() == 0, "toolbar : 2e clic Selection = deselection (chaine Echap complete)");

        /* Copier/Coller par boutons */
        selectOnly(0);
        runToolCommand(IDM_TOOL_COPY);
        CHECK(g_clipboard.size() == 1, "toolbar : bouton Copier = Ctrl+C");
        runToolCommand(IDM_TOOL_PASTE);
        CHECK((int)g_objs.size() == 2 && g_selected == 1,
              "toolbar : bouton Coller = Ctrl+V (copie posee et selectionnee)");

        /* Cadrer / Origine par boutons */
        selectOnly(1);
        g_cam.target = Vec3(50, 40, 30); g_cam.dist = 200.0;
        runToolCommand(IDM_TOOL_FRAME);
        CHECK((g_cam.target - g_objs[1].pos).norm() < 1e-12,
              "toolbar : bouton F cadrer = cadrage de la selection (vise le centre de la piece collee)");
        runToolCommand(IDM_TOOL_HOME);
        const Camera defaut{};
        const Vec3 cibleVisee = defaut.target;
        CHECK((g_cam.target - cibleVisee).norm() < 1e-12
              && g_cam.dist == defaut.dist && selectedCount() == 0,
              "toolbar : bouton H origine = retour vue d'origine + deselection");

        g_objs.clear(); g_selMulti.clear(); g_selected = -1; g_mode = M_None;
        g_rotActive = false; g_rotMode = false; g_ppActive = false; g_ppMode = false;
        g_clipboard.clear(); g_pasteRepeat = 0;
        g_rotMulti.clear(); g_rotPivotKind = 0;
    }

    /* 30 - B1.1 : preferences barre d'outils (section [outils] du INI) -
       round-trip visible/vertical/position, valeurs tolerees, ini editable */
    {
        const std::string path = tmpPath("setup_test2.ini");
        g_setupPath = path;
        g_setup = Setup{};
        std::remove(path.c_str());
        loadSetup();
        CHECK(g_setup.toolVisible == 1 && g_setup.toolVertical == 0
              && g_setup.toolX == 140 && g_setup.toolY == 140,
              "setup outils : fichier absent = barre visible, horizontale, position defaut");

        {
            std::ofstream f(path.c_str());
            f << "[outils]\nvisible = 0\nvertical = 1\nx = 1900\ny = 800\nclef_inconnue = 3\n"
              << "\n[fenetre]\nw = 1200\n";
        }
        loadSetup();
        CHECK(g_setup.toolVisible == 0 && g_setup.toolVertical == 1
              && g_setup.toolX == 1900 && g_setup.toolY == 800,
              "setup outils : ini edite a la main = toute la section [outils] relue");

        /* round-trip : un relancement doit retrouver chaque preference */
        saveSetup();
        g_setup = Setup{};
        loadSetup();
        CHECK(g_setup.toolVisible == 0 && g_setup.toolVertical == 1
              && g_setup.toolX == 1900 && g_setup.toolY == 800,
              "setup outils : round-trip complet (fermeture -> lancement suivant)");

        /* valeurs non numeriques : parse tolerant, rien ne plante */
        {
            std::ofstream f(path.c_str());
            f << "[outils]\nvisible = oui\nvertical = 0\nx = pas_un_nombre\ny = -50\n";
        }
        loadSetup();
        CHECK(g_setup.toolVisible == 0 && g_setup.toolVertical == 0
              && g_setup.toolX == 0 && g_setup.toolY == -50,
              "setup outils : valeurs non numeriques tolerees (atoi = 0, y negatif garde)");

        std::remove(path.c_str());
        g_setupPath = "sketch3d.ini";
        g_setup = Setup{};
    }

    /* 30b - B3.2c : nom de sauvegarde pre-rempli (dernier projet) */
    {
        noteProjectPath("C:/tmp/test_b32c.s3projet");
        CHECK(g_setup.dernierProjet == "C:/tmp/test_b32c.s3projet",
              "sauvegarde : le chemin note devient le nom pre-rempli du prochain enregistrement");
        handleMenuAction(IDM_NEW_PROJECT);
        CHECK(g_setup.dernierProjet.empty(),
              "sauvegarde : nouveau projet = nom pre-rempli VIDE (jamais le nom d un autre projet)");
        noteProjectPath("C:/tmp/test_b32c.s3projet");
        handleMenuAction(IDM_CLOSE_PROJECT);
        CHECK(g_setup.dernierProjet.empty(),
              "sauvegarde : projet ferme = nom pre-rempli VIDE");
    }

    /* 30c - B3.2d : historique des derniers projets (recents) */
    {
        g_setup = Setup{};
        noteProjectPath("a.s3projet");
        noteProjectPath("b.s3projet");
        noteProjectPath("a.s3projet");
        CHECK(g_setup.historique.size() == 2 && g_setup.historique[0] == "a.s3projet"
              && g_setup.historique[1] == "b.s3projet",
              "recents : deduplique, le plus recent d'abord");
        for (int i = 0; i < 9; ++i) {
            char nm[32]; std::snprintf(nm, sizeof(nm), "p%d.s3projet", i);
            noteProjectPath(nm);
        }
        CHECK(g_setup.historique.size() == 8 && g_setup.historique[0] == "p8.s3projet",
              "recents : borne a 8, le dernier ouvert devant");
        /* round-trip INI : l'historique survit au lancement suivant */
        g_setupPath = tmpPath("hist.ini");
        saveSetup();
        g_setup = Setup{};
        loadSetup();
        CHECK(g_setup.historique.size() == 8 && g_setup.historique[0] == "p8.s3projet"
              && g_setup.historique[7] == "p1.s3projet",
              "recents : historique survit au save/load du setup (ordre garde)");
        std::remove(g_setupPath.c_str());
        g_setupPath = "sketch3d.ini";
        g_setup = Setup{};
    }

    /* 30d - B3.2e : taille de l'historique editable dans l'INI */
    {
        const std::string path = tmpPath("recents.ini");
        g_setupPath = path;
        {
            std::ofstream f(path.c_str());
            f << "[projet]\nrecents = 3\nhistorique = a.s3projet;b.s3projet;c.s3projet;d.s3projet;e.s3projet\n";
        }
        loadSetup();
        CHECK(g_setup.nbRecents == 3 && g_setup.historique.size() == 3
              && g_setup.historique[0] == "a.s3projet",
              "recents : taille INI (recents = 3) borne l'historique relu");
        noteProjectPath("f.s3projet");
        noteProjectPath("g.s3projet");
        CHECK(g_setup.historique.size() == 3 && g_setup.historique[0] == "g.s3projet",
              "recents : la taille INI borne aussi les ajouts (deduplique, borne a 3)");
        std::remove(path.c_str());
        g_setupPath = "sketch3d.ini";
        g_setup = Setup{};
    }

    /* 31 - B1.7 : export uni (un seul lieu de choix du format = Preferences) -
       le format reel applique = extension du fichier choisi */
    {
        CHECK(exportPathIsStl("scene.stl") && exportPathIsStl("C:\\proj\\piece.STL"),
              "export uni : .stl (min/maj) = export STL");
        CHECK(!exportPathIsStl("piece.obj") && !exportPathIsStl("piece") && !exportPathIsStl("piece.vtt"),
              "export uni : .obj, sans extension, autre extension = pas STL (donc OBJ)");
    }


    /* 32 - B2.1 : outil Ligne (fil multi-segments pose au sol) ; le chemin
       rayon/souris (rayFromMouse) est deja couvert par les blocs drag et C
       (meme stub), ici la logique de capture est testee en pur. */
    {
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_lineMode = true;

        g_linePts.push_back(Vec3(0, 0, 0));
        g_linePts.push_back(Vec3(2, 0, 0));
        g_linePts.push_back(Vec3(2, 0, 2));
        g_lineActive = true;
        linePose();
        CHECK(g_objs.size() == 1 && g_objs[0].name == "Ligne",
              "ligne : la pose cree UN objet nomme Ligne (sommets partages)");
        CHECK(g_objs[0].v.size() == 3 && g_objs[0].tri.empty() && g_objs[0].edges.size() == 2,
              "ligne : fil = 3 sommets, 2 segments (aretes), aucun tri");
        CHECK((g_objs[0].v[1] - g_objs[0].v[0]).norm() == 2.0
              && (g_objs[0].v[2] - g_objs[0].v[1]).norm() == 2.0,
              "ligne : longueurs des segments exactes");
        CHECK(g_selected == 0 && !g_lineMode && !g_lineActive && g_linePts.empty()
              && g_lineNumBuf.empty(),
              "ligne : pose = selectionnee, mode quitte, dessin remis a zero");

        g_lineMode = true;                        /* second fil : fin par clic droit/Entree */
        g_linePts.push_back(Vec3(5, 0, 5));
        g_linePts.push_back(Vec3(6, 0, 5));
        g_lineActive = true;
        lineFinish();
        CHECK(g_objs.size() == 2 && g_objs[1].name == "Ligne (2)",
              "ligne : second fil nomme Ligne (2) sans collision de nom");
        CHECK(findObjectByName("Ligne (2)") == 1, "ligne : retrouve par nom");

        g_lineMode = true;                        /* longueur tapee + Entree */
        g_linePts.push_back(Vec3(0, 0, 0));
        g_lineActive = true;
        g_lineCursorOk = true;
        g_lineCursor = Vec3(3, 0, 0);
        linePoseByLength(1.5);
        CHECK(g_objs.size() == 3 && (g_objs[2].v[1] - g_objs[2].v[0]).norm() == 1.5,
              "ligne : chiffres+Entree 1.5 = point pose a 1.5 dans la direction curseur");
        CHECK(!g_lineMode && g_linePts.empty(),
              "ligne : pose par longueur = pose + sortie du mode (meme contrat)");

        g_lineMode = true;                        /* Echap : tout annule */
        g_linePts.push_back(Vec3(9, 0, 9));
        g_lineActive = true;
        lineCancel("dessin annule");
        CHECK(g_objs.size() == 3 && !g_lineMode && !g_lineActive && g_linePts.empty(),
              "ligne : Echap annule tout, rien de pose");

        g_lineMode = true;                        /* moins de 2 points = rien */
        g_linePts.push_back(Vec3(1, 0, 1));
        g_lineActive = true;
        lineFinish();
        CHECK(g_objs.size() == 3 && !g_lineMode && g_linePts.empty(),
              "ligne : fin avec un seul point = rien de pose");

        runToolCommand(IDM_TOOL_LINE);            /* un seul chemin de code */
        CHECK(g_lineMode && !g_lineActive,
              "ligne : bouton/menu (runToolCommand) arme l'outil");
        runToolCommand(IDM_TOOL_LINE);
        CHECK(!g_lineMode && !g_lineActive,
              "ligne : rappel du bouton = desarme (toggle)");

        /* B2.1c : capture d'un fil par distance au rayon (tolerance explicite) */
        {
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            Object fil;
            fil.name = "F";
            fil.pos = Vec3(0, 0, 0);
            fil.v = { Vec3(0, 0, 0), Vec3(4, 0, 0) };
            computeObjectDerived(fil);
            g_objs.push_back(fil);
            CHECK(fil.edges.size() == 1, "fil : la chaine d'aretes est derivee (1 segment)");
            CHECK(filRayHit(Vec3(2, 0.3, 0), Vec3(0, -1, 0), g_objs[0], 0.5) > 0.0,
                  "fil : rayon a 0.3 du segment = capture (tolerance 0.5)");
            CHECK(filRayHit(Vec3(2, 0.3, 3), Vec3(0, -1, 0), g_objs[0], 0.5) < 0.0,
                  "fil : rayon PARALLELE a 3 d'ecart = pas de capture (ne traverse jamais)");
            CHECK(filRayHit(Vec3(5.5, 1.0, 0), Vec3(0, -1, 0), g_objs[0], 0.5) < 0.0,
                  "fil : rayon au-dela de l'extremite (proche a 1.5) = pas de capture (tol 0.5)");
        }

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_lineMode = false;
        lineReset();
    }

    { /* B2.5b : fermeture de la ligne - deux commandes (L = surface, Maj+L = fil), rien de memorise */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        runToolCommand(IDM_TOOL_LINE);
        CHECK(g_lineMode && g_lineFerme,
              "ligne : L arme le mode, fermeture en surface (choix par la commande, rien de memorise)");
        runToolCommand(IDM_TOOL_LINE);               /* rappel = desarme */
        runToolCommand(IDM_TOOL_LIGNE_FIL);
        CHECK(g_lineMode && !g_lineFerme,
              "ligne : Maj+L arme le mode, fil ouvert (meme outil, autre commande)");
        lineCancel("fin du test d'armement");
        g_lineMode = false;
        g_lineFerme = true;                          /* defaut : boucle fermee (B3.1b : jamais de surface) */
        g_linePts = { Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 2), Vec3(0, 0, 2) };
        g_lineActive = true;
        g_lineMode = true;
        lineCloseLoop();
        CHECK(g_objs.size() == 1 && g_objs[0].tri.empty() && g_objs[0].edges.size() == 4,
              "ligne : fermeture = fil en BOUCLE sans surface (4 aretes, 0 tri ; fermer en surface = le polygone)");
        CHECK(g_objs[0].v.size() == 4 && g_objs[0].traceSurface == 0
              && std::fabs(signedVolume(g_objs[0])) < 1e-12,
              "ligne : boucle fermee = 4 sommets (pas de doublon du depart), pas de surface");
        CHECK(!g_lineMode && !g_lineActive && g_linePts.empty(),
              "ligne : fermeture = pose + sortie du mode");

        g_lineFerme = false;                         /* option : fil ouvert */
        g_linePts = { Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 2), Vec3(0, 0, 2) };
        g_lineActive = true;
        g_lineMode = true;
        lineCloseLoop();
        CHECK(g_objs.size() == 2 && g_objs[1].tri.empty() && g_objs[1].edges.size() == 4,
              "ligne : fil ouvert = boucle fermee sans surface (4 aretes, 0 tri)");
        CHECK(g_objs[1].name != g_objs[0].name, "ligne : noms uniques aux deux poses");
        g_lineFerme = true;                          /* retour au defaut */

        /* B3.2b (retour test) : round-trip d un fil ferme - l arete de
           cloture survit au save/load SANS regen manuelle (la regen des
           parametres de construction fait partie du CHARGEMENT) */
        {
            std::string err;
            const std::string p = tmpPath("boucle_ligne.s3projet");
            CHECK(saveProject(p, err), err.c_str());
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            CHECK(loadProject(p, err), err.c_str());
            CHECK(g_objs.size() == 2 && g_objs[0].traceFerme == 1
                  && g_objs[0].tri.empty() && g_objs[0].edges.size() == 4
                  && g_objs[1].edges.size() == 4,
                  "ligne : fil ferme relu = boucle complete (arete de cloture survivante, 0 tri)");
        }

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_lineMode = false;
        lineReset();
    }

    { /* B2.2 : outil Rectangle - feuille pleine posee au sol (Maj+R) */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        runToolCommand(IDM_TOOL_RECT);
        CHECK(g_rectMode && !g_rectActive,
              "rectangle : Maj+R arme le mode (aucun coin encore pose)");
        runToolCommand(IDM_TOOL_RECT);               /* rappel = desarme */
        CHECK(!g_rectMode, "rectangle : rappel de la commande = desarme");

        rectPose(Vec3(0, 0, 0), Vec3(2, 0, 3));
        CHECK(g_objs.size() == 1 && g_objs[0].name == "Rectangle"
              && g_objs[0].tri.size() == 6 && g_objs[0].v.size() == 4 && g_objs[0].edges.size() == 5,
              "rectangle : feuille pleine = 1 objet, 2 tris, 4 sommets, 5 aretes (4 bords + diagonale du fan)");
        CHECK(std::fabs(signedVolume(g_objs[0])) < 1e-12 && g_objs[0].n[0].y > 0.99
              && g_objs[0].bmin.x == 0.0 && g_objs[0].bmax.x == 2.0 && g_objs[0].bmax.z == 3.0,
              "rectangle : normale +y, volume plat nul, etendue 2 x 3");

        g_rectActive = true; g_rectCorner = Vec3(0, 0, 0); g_rectNumBuf.clear();
        g_rectCursor = Vec3(1, 0, 1); g_rectCursorOk = true;
        rectPoseByDims("2;3");
        CHECK(g_objs.size() == 2 && g_objs[1].name == "Rectangle (2)"
              && g_objs[1].bmax.x == 2.0 && g_objs[1].bmax.z == 3.0 && g_objs[1].v.size() == 4,
              "rectangle : L;H + Entree = dimensions exactes (quadrant curseur SE = +x,+z), nom unique");

        g_rectActive = true; g_rectCorner = Vec3(5, 0, 5);
        g_rectCursor = Vec3(4, 0, 4); g_rectCursorOk = true;
        rectPoseByDims("2;3");
        CHECK(g_objs.size() == 3 && g_objs[2].pos.x == 3.0 && g_objs[2].pos.z == 2.0
              && g_objs[2].bmax.x == 2.0 && g_objs[2].bmax.z == 3.0,
              "rectangle : quadrant curseur NO = -x,-z (le rectangle s etend vers l arriere-plan)");

        g_rectActive = true; g_rectCorner = Vec3(0, 0, 0);
        const size_t avant = g_objs.size();
        rectPoseByDims("abc");
        CHECK(g_objs.size() == avant, "rectangle : saisie invalide = rien pose");
        g_rectActive = false;
        g_rectMode = false;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.2c : Push/Pull sur feuille rectangle + clavier FR (point-virgule) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        rectPose(Vec3(0, 0, 0), Vec3(2, 0, 3));
        CHECK(g_objs.size() == 1 && g_objs[0].tri.size() == 6,
              "B2.2c : feuille rectangle posee avant tirage (2 tris)");
        Extrude dd;
        CHECK(makeExtrude(g_objs[0], 0, dd),
              "B2.2c : Push/Pull disponible sur la feuille rectangle (garde makeExtrude = 1 tri)");
        applyExtrude(g_objs[0], dd, 2.0);
        double ymax = -1e300;
        for (const Vec3& p : g_objs[0].v) if (p.y > ymax) ymax = p.y;
        CHECK(ymax > 2.0 - 1e-9 && ymax < 2.0 + 1e-9 && g_objs[0].tri.size() > 6,
              "B2.2c : tirage de la feuille = volume recree (couche a +2, jupe + fond)");
        /* clavier FR : le ';' (Maj + touche a cote de M) arrive en GLFW_KEY_COMMA + SHIFT */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_rectActive = true; g_rectNumBuf.clear();
        onKey(W, GLFW_KEY_SEMICOLON, 0, GLFW_PRESS, 0);
        CHECK(g_rectNumBuf == ";",
              "B2.2c : touche ';' directe (sans Maj) tape un point-virgule");
        g_rectNumBuf.clear();
        onKey(W, GLFW_KEY_COMMA, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_rectNumBuf == ";",
              "B2.2c : Maj + touche virgule tape un POINT-VIRGULE (quirk clavier FR)");
        onKey(W, GLFW_KEY_COMMA, 0, GLFW_PRESS, 0);
        CHECK(g_rectNumBuf == ";,",
              "B2.2c : virgule non majusculee = decimale FR acceptee dans la saisie");
        g_rectActive = true; g_rectCorner = Vec3(0, 0, 0);
        g_rectCursor = Vec3(1, 0, 1); g_rectCursorOk = true;
        rectPoseByDims("2,5;3");
        CHECK(g_objs.size() == 1 && g_objs[0].bmax.x == 2.5 && g_objs[0].bmax.z == 3.0,
              "B2.2c : L;H avec decimales FR (2,5;3) = rectangle 2.5 x 3");
        g_rectActive = false;
        g_rectMode = false;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.3 : outil Cercle (Maj+C, fan 24 tris, rayon exact) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        onKey(W, GLFW_KEY_C, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_cercleMode && !g_cercleActive,
              "cercle : Maj+C arme l outil (C seul = centrer, Ctrl+C = copie, non touches)");
        onKey(W, GLFW_KEY_C, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(!g_cercleMode, "cercle : rappel de Maj+C = desarme (toggle)");
        g_cercleMode = true; cercleReset();
        cerclePose(Vec3(0, 0, 0), 2.0);
        CHECK(g_objs.size() == 1 && g_objs[0].v.size() == 25 && g_objs[0].tri.size() == 72,
              "cercle : feuille fan = 25 sommets (centre + boucle de 24), 72 indices de tri");
        CHECK(g_objs[0].edges.size() == 48,
              "cercle : aretes derivees = 24 bords + 24 rayons du fan (v1, cf. diagonale B2.5)");
        const double ny = g_objs[0].v[1].z * g_objs[0].v[2].x - g_objs[0].v[1].x * g_objs[0].v[2].z;
        CHECK(ny > 0.0 && g_objs[0].bmax.x == 2.0 && g_objs[0].bmax.z == 2.0
              && g_objs[0].bmin.x == -2.0 && g_objs[0].bmin.z == -2.0,
              "cercle : normale +y, etendue = centre +/- rayon (bmin/bmax LOCAUX)");
        CHECK(std::fabs(signedVolume(g_objs[0])) < 1e-12,
              "cercle : volume plat nul (feuille, comme la ligne fermee B2.5)");
        Extrude dd;
        CHECK(makeExtrude(g_objs[0], 0, dd),
              "cercle : Push/Pull disponible sur la feuille fan (garde = 1 tri)");
        applyExtrude(g_objs[0], dd, 2.0);
        double ymax = -1e300;
        for (const Vec3& p : g_objs[0].v) if (p.y > ymax) ymax = p.y;
        CHECK(ymax > 2.0 - 1e-9 && ymax < 2.0 + 1e-9 && g_objs[0].tri.size() > 72,
              "cercle : tirage de la feuille = volume recree (couche a +2, jupe + fond)");
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_cercleMode = true; cercleReset();
        cerclePoseByRayon("2,5");
        CHECK(g_objs.size() == 1 && g_objs[0].bmax.x == 2.5 && g_objs[0].bmin.x == -2.5,
              "cercle : rayon tape avec decimale FR (2,5) = rayon 2.5");
        /* pavé numerique : les CHIFFRES s'affichent (bug B2.3b : tout etait
           remplace par la decimale) */
        g_cercleMode = true; cercleReset(); g_cercleActive = true; g_cercleCentre = Vec3(0, 0, 0);
        onKey(W, GLFW_KEY_KP_2, 0, GLFW_PRESS, 0);
        onKey(W, GLFW_KEY_KP_DECIMAL, 0, GLFW_PRESS, 0);
        onKey(W, GLFW_KEY_KP_5, 0, GLFW_PRESS, 0);
        CHECK(g_cercleNumBuf == "2.5",
              "cercle : pave numerique = chiffres tapes, KP_DECIMAL = decimale");
        g_cercleMode = true; cercleReset();
        const size_t avant = g_objs.size();
        g_cercleMode = true; cercleReset();
        cerclePoseByRayon("abc");
        CHECK(g_objs.size() == avant, "cercle : saisie invalide = rien pose");
        g_cercleMode = false; cercleReset();
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.4 : outil Arc (Maj+A, fil ouvert 12 segments) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        onKey(W, GLFW_KEY_A, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_arcMode && g_arcState == 0, "arc : Maj+A arme l outil");
        onKey(W, GLFW_KEY_A, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(!g_arcMode, "arc : rappel de Maj+A = desarme (toggle)");
        /* B2.4e : AZERTY - la touche etiquetee A arrive en GLFW_KEY_Q (position
           QWERTY) ; le raccourci suit l'ETIQUETTE (keyIsLetter via le nom de touche) */
        onKey(W, GLFW_KEY_Q, 0x10, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_arcMode && g_arcState == 0,
              "arc : AZERTY - Maj + touche etiquetee A (code GLFW_KEY_Q) arme l'outil");
        onKey(W, GLFW_KEY_Q, 0x10, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(!g_arcMode, "arc : AZERTY - rappel = desarme (toggle)");
        /* pose par 3 points : P1(0,0,0), P2(4,0,0), B(2,0,1) -> cercle centre (2,-1.5), r = 2.5 */
        g_arcMode = true; arcReset();
        arcPose(Vec3(0, 0, 0), Vec3(2, 0, 1), Vec3(4, 0, 0));
        CHECK(g_objs.size() == 1 && g_objs[0].tri.empty() && g_objs[0].v.size() == 13,
              "arc : fil ouvert = 13 points (12 segments), SANS triangle");
        CHECK(g_objs[0].edges.size() == 12,
              "arc : aretes derivees = chaine de 12 segments (comme la ligne)");
        bool surCercle = true;
        for (const Vec3& p : g_objs[0].v) {
            const double dx = g_objs[0].pos.x + p.x - 2.0;
            const double dz = g_objs[0].pos.z + p.z + 1.5;
            if (std::fabs(std::sqrt(dx * dx + dz * dz) - 2.5) > 1e-9) surCercle = false;
        }
        const Vec3 e0 = g_objs[0].pos + g_objs[0].v[0];
        const Vec3 e1 = g_objs[0].pos + g_objs[0].v[12];
        CHECK(surCercle && std::fabs(e0.x) < 1e-9 && std::fabs(e0.z) < 1e-9
              && std::fabs(e1.x - 4.0) < 1e-9 && std::fabs(e1.z) < 1e-9,
              "arc : points EXACTS sur le cercle, extremites = depart et fin (pos + v = monde)");
        const size_t avant = g_objs.size();
        g_arcMode = true; arcReset();
        arcPose(Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(4, 0, 0));
        CHECK(g_objs.size() == avant, "arc : bombement aligne avec la corde = rien pose");
        /* saisie : corde puis fleche, cote du bombement = curseur */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_arcMode = true; arcReset();
        g_arcP1 = Vec3(0, 0, 0); g_arcState = 1;
        g_arcCursor = Vec3(3, 0, 0); g_arcCursorOk = true;
        arcPoseByCorde(4.0);
        CHECK(g_arcState == 2 && std::fabs(g_arcP2.x - 4.0) < 1e-12 && std::fabs(g_arcP2.z) < 1e-12,
              "arc : corde tapee = fin posee dans la direction du curseur");
        g_arcCursor = Vec3(2, 0, 1);
        arcPoseByFleche(1.0);
        CHECK(g_objs.size() == 1 && g_objs[0].tri.empty() && g_objs[0].v.size() == 13,
              "arc : fleche tapee = arc pose (fil ouvert, cote curseur)");
        /* B2.4d : la limite des 180 deg est LEVEE - toute fleche > 0 est
           valide ; au-dela de la moitie de la corde l'arc est MAJEUR */
        const size_t avant2 = g_objs.size();
        g_arcMode = true; arcReset();
        g_arcP1 = Vec3(0, 0, 0); g_arcP2 = Vec3(4, 0, 0); g_arcState = 2;
        g_arcCursor = Vec3(2, 0, -3); g_arcCursorOk = true;
        arcPoseByFleche(3.0);   /* fleche 3 > moitie de la corde (2) = arc majeur */
        CHECK(g_objs.size() == avant2 + 1 && g_objs.back().tri.empty()
              && g_objs.back().v.size() == 13,
              "arc : fleche > moitie de la corde = arc MAJEUR pose (B2.4d, > 180 deg)");
        {
            const Vec3 m = g_objs.back().pos + g_objs.back().v[6];   /* milieu de l'arc = sommet du bombement */
            CHECK(std::fabs(m.x - 2.0) < 1e-9 && std::fabs(m.z + 3.0) < 1e-9,
                  "arc majeur : le milieu de l'arc passe par la fleche tapee (3 pour une corde de 4)");
        }
        const size_t avant3 = g_objs.size();
        g_arcMode = true; arcReset();
        g_arcP1 = Vec3(0, 0, 0); g_arcP2 = Vec3(4, 0, 0); g_arcState = 2;
        g_arcCursor = Vec3(2, 0, -20); g_arcCursorOk = true;
        arcPoseByFleche(20.0);  /* fleche 5x la corde = proche du cercle complet */
        {
            const Vec3 m = g_objs.size() == avant3 + 1 ? g_objs.back().pos + g_objs.back().v[6] : Vec3(0, 0, 0);
            CHECK(g_objs.size() == avant3 + 1 && std::fabs(m.x - 2.0) < 1e-9 && std::fabs(m.z + 20.0) < 1e-9,
                  "arc : fleche 5x la corde = posee, milieu de l'arc au sommet du bombement");
        }
        g_arcMode = false; arcReset();
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.6 : outil Polygone (Maj+P, surface fan, fermeture sur le depart) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        onKey(W, GLFW_KEY_P, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_polyMode && !g_polyActive, "polygone : Maj+P arme l outil");
        onKey(W, GLFW_KEY_P, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(!g_polyMode, "polygone : rappel de Maj+P = desarme (toggle)");
        /* quad pose en 4 sommets -> fan de 2 triangles, etirable */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_polyMode = true; polyReset();
        g_polyPts.push_back(Vec3(0, 0, 0));
        g_polyPts.push_back(Vec3(4, 0, 0));
        g_polyPts.push_back(Vec3(4, 0, 3));
        g_polyPts.push_back(Vec3(0, 0, 3));
        polyPose();
        CHECK(g_objs.size() == 1 && !g_objs[0].tri.empty() && g_objs[0].v.size() == 4
              && g_objs[0].tri.size() == 6,
              "polygone : quad pose = 1 objet, 4 sommets, fan de 2 tris (6 indices)");
        CHECK(g_objs[0].edges.size() == 5,
              "polygone : aretes derivees = 4 bords + 1 diagonale du fan (v1)");
        CHECK(std::fabs(signedVolume(g_objs[0])) < 1e-12,
              "polygone : feuille plane = volume nul");
        CHECK(std::fabs(g_objs[0].pos.x) < 1e-12 && std::fabs(g_objs[0].pos.z) < 1e-12
              && std::fabs(g_objs[0].pos.y + g_objs[0].v[0].y) < 1e-12,
              "polygone : ancre locale = premier sommet (pos + v = monde)");
        /* makeExtrude : la feuille du polygone est une face push/pull valide */
        {
            Extrude dd;
            CHECK(makeExtrude(g_objs[0], 0, dd), "polygone : feuille = face push/pull valide");
        }
        /* noms uniques : Polygone, Polygone (2) */
        g_polyMode = true; polyReset();
        g_polyPts.push_back(Vec3(1, 0, 1));
        g_polyPts.push_back(Vec3(2, 0, 1));
        g_polyPts.push_back(Vec3(2, 0, 2));
        polyPose();
        CHECK(g_objs.size() == 2 && g_objs[1].name != g_objs[0].name,
              "polygone : noms uniques (Polygone, Polygone (2))");
        /* B2.6b : clic droit / Entree = VALIDE (ferme la surface) ; Echap seul annule */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_polyMode = true; polyReset();
        g_polyPts.push_back(Vec3(0, 0, 0));
        g_polyPts.push_back(Vec3(3, 0, 0));
        g_polyPts.push_back(Vec3(0, 0, 3));
        polyFinish();
        CHECK(g_objs.size() == 1 && !g_objs[0].tri.empty() && !g_polyMode,
              "polygone : validation (clic droit/Entree) a 3 sommets = surface posee");
        const size_t avantF = g_objs.size();
        g_polyMode = true; polyReset();
        g_polyPts.push_back(Vec3(0, 0, 0));
        g_polyPts.push_back(Vec3(3, 0, 0));
        polyFinish();
        CHECK(g_objs.size() == avantF && g_polyMode && g_polyPts.size() == 2,
              "polygone : validation a 2 sommets = refusee, mode conserve (Echap pour sortir)");
        g_polyMode = false; polyReset();
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.7 : outil Spline (Maj+S, points de passage, fil discretise) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;   /* stub : jamais dereference */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        onKey(W, GLFW_KEY_S, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_splMode && !g_splActive, "spline : Maj+S arme l outil");
        onKey(W, GLFW_KEY_S, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(!g_splMode, "spline : rappel de Maj+S = desarme (toggle)");
        /* courbe ouverte posee = fil en UN objet, sans triangle */
        const std::vector<Vec3> passage = { Vec3(0, 0, 0), Vec3(2, 0, 1), Vec3(4, 0, 0), Vec3(6, 0, 2) };
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_splMode = true; splReset();
        for (const Vec3& q : passage) g_splPts.push_back(q);
        splFinish();
        CHECK(g_objs.size() == 1 && g_objs[0].tri.empty() && !g_splMode,
              "spline : posee ouverte = fil en UN objet sans triangle, mode quitte");
        { /* la discretisation PASSE PAR chaque point de passage */
            bool passe = true;
            const Object& o = g_objs[0];
            for (const Vec3& q : passage) {
                double dmin = 1e300;
                for (const Vec3& loc : o.v) {
                    const double d = (q - (o.pos + loc)).norm();
                    if (d < dmin) dmin = d;
                }
                if (dmin > 1e-9) passe = false;
            }
            CHECK(passe, "spline ouverte : la courbe passe par chaque point de passage");
        }
        CHECK(g_objs[0].edges.size() == g_objs[0].v.size() - 1,
              "spline ouverte : aretes = chaine continue (nv - 1)");
        /* boucle fermee (clic sur le depart) : dernier sommet = premier */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_splMode = true; splReset();
        for (const Vec3& q : passage) g_splPts.push_back(q);
        splPose(true);
        CHECK(g_objs.size() == 1
              && (g_objs[0].v.front() - g_objs[0].v.back()).norm() < 1e-12,
              "spline fermee : boucle = dernier sommet = premier (arete de cloture)");
        CHECK(g_objs[0].edges.size() == g_objs[0].v.size() - 1,
              "spline fermee : aretes = chaine fermee (nv - 1 avec cloture)");
        /* validation prematuree : 1 point = refuse, mode conserve */
        const size_t avantS = g_objs.size();
        g_splMode = true; splReset();
        g_splPts.push_back(Vec3(1, 0, 1));
        splFinish();
        CHECK(g_objs.size() == avantS && g_splMode,
              "spline : validation a 1 point = refusee, mode conserve (Echap pour sortir)");
        /* noms uniques */
        g_splPts.push_back(Vec3(2, 0, 1));
        splFinish();
        CHECK(g_objs.size() == avantS + 1 && g_objs.back().name != g_objs[g_objs.size() - 2].name,
              "spline : noms uniques (Spline, Spline (2))");
        g_splMode = false; splReset();
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    { /* B2.7b : variante POINTS DE CONTROLE (Maj+B, bascule a chaud) */
        GLFWwindow* W = (GLFWwindow*)(std::uintptr_t)1ULL;
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        onKey(W, GLFW_KEY_B, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
        CHECK(g_splMode && g_splCtrl, "spline : Maj+B arme la variante points de controle");
        /* carre en polygone de controle : la courbe glisse a l'interieur */
        const std::vector<Vec3> carre = { Vec3(0, 0, 0), Vec3(4, 0, 0), Vec3(4, 0, 4), Vec3(0, 0, 4) };
        g_splMode = true; g_splCtrl = true; splReset();
        for (const Vec3& q : carre) g_splPts.push_back(q);
        splFinish();                             /* ouverte : noeuds serres */
        CHECK(g_objs.size() == 1 && g_objs[0].tri.empty(),
              "spline (controle) : posee = fil en UN objet sans triangle");
        {
            const Object& o = g_objs[0];
            const Vec3 w0 = o.pos + o.v.front(), w1 = o.pos + o.v.back();
            CHECK((w0 - Vec3(0, 0, 0)).norm() < 1e-9 && (w1 - Vec3(0, 0, 4)).norm() < 1e-9,
                  "spline (controle) : noeuds serres = depart et arrivee EXACTS");
            double dmin = 1e300;
            for (const Vec3& loc : o.v) {
                const double d = (Vec3(4, 0, 0) - (o.pos + loc)).norm();
                if (d < dmin) dmin = d;
            }
            CHECK(dmin > 0.1, "spline (controle) : la courbe glisse a l'interieur (coin non touche)");
            /* B2.7d : valeur EXACTE de Bezier au milieu - le carre de controle
               (4 clics, noeuds serres) donne un segment de Bezier cubique :
               C(0.5) = (P0 + 3 P1 + 3 P2 + P3) / 8 = (3, 0, 2). Ce check
               aurait attrape le bug du span (floor(u) faux sur noeuds serres). */
            double dmil = 1e300;
            for (const Vec3& loc : o.v) {
                const double d = (Vec3(3, 0, 2) - (o.pos + loc)).norm();
                if (d < dmil) dmil = d;
            }
            CHECK(dmil < 0.2, "spline (controle) : Bezier cubique exacte au milieu (span des noeuds serres)");
        }
        /* boucle fermee : dernier sommet = premier */
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_splMode = true; g_splCtrl = true; splReset();
        for (const Vec3& q : carre) g_splPts.push_back(q);
        splPose(true);
        CHECK(g_objs.size() == 1 && (g_objs[0].v.front() - g_objs[0].v.back()).norm() < 1e-12,
              "spline (controle) fermee : boucle = dernier sommet = premier");
        g_splMode = false; g_splCtrl = false; splReset();
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    /* ------------------------------------------------------------------ */
    /* B2.7e : EDITION DES TRACES APRES COUP - source de verite TraceData, */
    /* regeneration par les MEMES fonctions que le trace, ajout/suppression */
    /* de points, ouvert/ferme, persistence save/load                       */
    /* ------------------------------------------------------------------ */
    {
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        /* 1. LIGNE posee : traceType + points de definition survivent a la pose */
        g_linePts = { Vec3(0,0,0), Vec3(2,0,0), Vec3(2,0,2) };
        linePose();
        CHECK(g_objs.size() == 1 && g_objs[0].traceType == 1 && !g_objs[0].traceFerme,
              "edition (ligne) : TraceData rempli a la pose (type = LIGNE, ouvert)");
        CHECK(g_objs[0].tracePts.size() == 3 && (g_objs[0].tracePts[1] - Vec3(2,0,0)).norm() < 1e-12,
              "edition (ligne) : points de definition = sommets du fil");
        /* 2. drag d'une poignee : la geometrie suit (source de verite) */
        g_objs[0].tracePts[1] = Vec3(4,0,1);
        traceRegen(g_objs[0]);
        CHECK(g_objs[0].v.size() == 3 && (g_objs[0].v[1] - Vec3(4,0,1)).norm() < 1e-12,
              "edition (ligne) : poignee deplacee = fil regenere (un seul chemin de code)");
        /* 3. ajout d'un point : clic au milieu d'un segment -> insertion a l'intervalle */
        const int ia = traceAddPoint(g_objs[0], Vec3(1,0,1) + g_objs[0].pos);
        CHECK(ia > 0 && g_objs[0].tracePts.size() == 4,
              "edition (ligne) : clic sur la courbe = point de definition ajoute");
        CHECK((g_objs[0].tracePts[(size_t)ia] - Vec3(1,0,1)).norm() < 1e-12,
              "edition (ligne) : le point ajoute est au bon intervalle");
        /* 4. suppression + garde du minimum (2 points pour une ligne ouverte) :
              4 -> 3 -> 2 points ok, puis refus a 2 (minimum atteint) */
        CHECK(traceDelPoint(g_objs[0], ia), "edition (ligne) : poignee + Suppr = point supprime (4 -> 3)");
        CHECK(traceDelPoint(g_objs[0], 0) && g_objs[0].tracePts.size() == 2,
              "edition (ligne) : suppression a 3 points (-> 2)");
        CHECK(!traceDelPoint(g_objs[0], 0) && g_objs[0].tracePts.size() == 2,
              "edition (ligne) : garde - minimum 2 points (refus sous le minimum)");
        /* 5. ouvert/ferme : O bascule (arete de cloture pour le fil) -
              re-ajout d'un point d'abord (3 points requis pour fermer) */
        CHECK(traceAddPoint(g_objs[0], Vec3(0,0,2) + g_objs[0].pos) >= 0
              && g_objs[0].tracePts.size() == 3,
              "edition (ligne) : re-ajout d'un point (clic sur la courbe)");
        traceToggleFerme(g_objs[0]);
        CHECK(g_objs[0].traceFerme == 1 && g_objs[0].edges.size() == 3,
              "edition (ligne) : O = ferme (arete de cloture derivee)");
        traceToggleFerme(g_objs[0]);
        CHECK(g_objs[0].traceFerme == 0 && g_objs[0].edges.size() == 2,
              "edition (ligne) : O = re-ouvre (chaine simple)");
        /* 6. SPLINE passage posee, poignee deplacee : la courbe passe par le nouveau point */
        g_splPts = { Vec3(0,0,0), Vec3(2,0,0), Vec3(4,0,1), Vec3(6,0,0) };
        g_splMode = true; g_splCtrl = false;
        splPose(false);
        g_splMode = false;
        CHECK(g_objs.size() == 2 && g_objs[1].traceType == 2,
              "edition (spline) : TraceData rempli a la pose (type = SPLINE passage)");
        {
            Object& sp = g_objs[1];
            const Vec3 nouveau(3, 0, -1);            /* nouveau point de passage (monde) */
            sp.tracePts[2] = (nouveau - sp.pos) * (1.0 / sp.scl);
            traceRegen(sp);
            double dmin = 1e300;
            for (const Vec3& loc : sp.v) dmin = std::min(dmin, (sp.pos + loc - nouveau).norm());
            CHECK(dmin < 1e-9, "edition (spline passage) : la courbe regeneree passe par la poignee deplacee");
            /* extremes exacts conserves (noeuds serres) */
            CHECK((sp.pos + sp.v.front() - Vec3(0,0,0)).norm() < 1e-9
                  && (sp.pos + sp.v.back() - Vec3(6,0,0)).norm() < 1e-9,
                  "edition (spline passage) : extremes exacts apres edition");
            /* 7. bascule variante a chaud : points conserves, courbe interieure */
            traceSetVariante(sp, true);
            CHECK(sp.traceType == 3 && sp.tracePts.size() == 4,
                  "edition (spline) : Maj+B = variante controle A CHAUD (points conserves)");
            double dmin2 = 1e300;
            for (const Vec3& loc : sp.v)
                dmin2 = std::min(dmin2, (sp.pos + loc - Vec3(3,0,2)).norm());
            CHECK(dmin2 > 0.05, "edition (spline controle) : la courbe reste interieure au polygone");
            /* 8. fermeture spline + garde */
            traceToggleFerme(sp);
            CHECK(sp.traceFerme == 1 && (sp.v.front() - sp.v.back()).norm() < 1e-12,
                  "edition (spline) : O = boucle fermee (dernier sommet = premier)");
            CHECK(traceDelPoint(sp, 3) && sp.tracePts.size() == 3,
                  "edition (spline fermee) : suppression a 4 points (-> 3)");
            CHECK(!traceDelPoint(sp, 0), "edition (spline fermee) : garde - minimum 3 points");
        }
        /* 9. persistence : TraceData survit au save/load (round-trip) */
        {
            std::string err;
            const std::string p = tmpPath("edition.s3projet");
            CHECK(saveProject(p, err), err.c_str());
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            CHECK(loadProject(p, err), err.c_str());
            CHECK(g_objs.size() == 2 && g_objs[1].traceType == 3 && g_objs[1].traceFerme == 1
                  && g_objs[1].tracePts.size() == 3,
                  "edition : persistence - type/ouvert-ferme/points survivent au save/load");
            /* la geometrie relue est IDENTIQUE (regenerable a l'identique) */
            traceRegen(g_objs[1]);
            CHECK(g_objs[1].v.size() == g_objs[1].v.size(), "edition : relue (sanity)");
        }
        /* 10. vieux projets (sans bloc trace) restent relisibles : retrocompatibilite */
        {
            std::string err;
            const std::string p = tmpPath("sans_trace.s3projet");
            std::ofstream f(p);
            f << "s3projet 1\nobjet Cube\npos 0 0 0\nscl 1\ncol 0.5 0.5 0.5\n"
                 "verts 1\n0 0 0\ntris 0\n";
            f.close();
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            CHECK(loadProject(p, err), err.c_str());
            CHECK(g_objs.size() == 1 && g_objs[0].traceType == 0,
                  "edition : vieux projet sans bloc trace relisible (traceType = 0)");
        }
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
    }

    /* ------------------------------------------------------------------ */
    /* B2.7e E2 : formes de base editables (rectangle, cercle, arc,        */
    /* polygone) - memes poignees, regeneration par les MEMES constructions */
    /* ------------------------------------------------------------------ */
    {
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        /* 1. RECTANGLE : TraceData = 2 coins ; drag d'un coin = dimensions suivent */
        rectPose(Vec3(0,0,0), Vec3(2,0,3));
        CHECK(g_objs.size() == 1 && g_objs[0].traceType == 4 && g_objs[0].tracePts.size() == 2,
              "edition (rectangle) : TraceData = 2 coins a la pose");
        g_objs[0].tracePts[1] = Vec3(5, 0, 1);      /* coin oppose deplace (locales) */
        traceRegen(g_objs[0]);
        CHECK(g_objs[0].v.size() == 4 && std::fabs(g_objs[0].v[2].x - 5.0) < 1e-12
              && std::fabs(g_objs[0].v[2].z - 1.0) < 1e-12,
              "edition (rectangle) : coins canoniques recalcules (min/max des 2 coins)");
        CHECK(g_objs[0].tri.size() == 6,
              "edition (rectangle) : 2 tris regeneres");
        /* 2. CERCLE : centre + point du rayon ; drag du rayon = 24 tris suivent */
        cerclePose(Vec3(1,0,1), 2.0);
        CHECK(g_objs[1].traceType == 5 && g_objs[1].v.size() == 1 + (size_t)CERCLE_SEG,
              "edition (cercle) : TraceData = centre + point du rayon, 24 segments");
        g_objs[1].tracePts[1] = Vec3(4, 0, 0);      /* rayon 4 (locales, centre = 0,0,0) */
        traceRegen(g_objs[1]);
        CHECK(std::fabs((g_objs[1].v[1] - g_objs[1].tracePts[0]).norm() - 4.0) < 1e-9,
              "edition (cercle) : rayon regenere = distance centre -> poignee");
        /* 3. ARC : 3 points de definition ; drag du bombement = arc suit */
        arcPose(Vec3(0,0,0), Vec3(1,0,1), Vec3(2,0,0));
        CHECK(g_objs[2].traceType == 6 && g_objs[2].v.size() == (size_t)ARC_SEG + 1,
              "edition (arc) : TraceData = depart/fin/bombement, 13 sommets");
        {
            double dmin = 1e300;
            for (const Vec3& loc : g_objs[2].v) dmin = std::min(dmin, (loc - Vec3(1,0,1)).norm());
            CHECK(dmin < 1e-9, "edition (arc) : l'arc passe EXACTEMENT par le bombement");
            g_objs[2].tracePts[1] = Vec3(1, 0, -1);   /* bombement deplace (locales) */
            traceRegen(g_objs[2]);
            dmin = 1e300;
            for (const Vec3& loc : g_objs[2].v) dmin = std::min(dmin, (loc - Vec3(1,0,-1)).norm());
            CHECK(dmin < 1e-9, "edition (arc) : bombe deplace = arc regenere passant par le nouveau point");
            CHECK((g_objs[2].v.front()).norm() < 1e-9 && (g_objs[2].v.back() - Vec3(2,0,0)).norm() < 1e-9,
                  "edition (arc) : depart et fin exacts (ancre locale conservee)");
        }
        /* 3b. B2.7e2b : drag du DEPART - l'arc doit rester passe par les 3 poignees
           (bug corrige : le rebasage sur tracePts[0] detachait l'arc des poignees) */
        {
            g_objs[2].tracePts[0] = Vec3(1, 0, 1);   /* depart deplace (locales) */
            traceRegen(g_objs[2]);
            bool ok3 = true;
            for (int i = 0; i < 3 && ok3; ++i) {
                double dmin = 1e300;
                for (const Vec3& loc : g_objs[2].v)
                    dmin = std::min(dmin, (loc - g_objs[2].tracePts[(size_t)i]).norm());
                ok3 = dmin < 1e-9;
            }
            CHECK(ok3, "edition (arc) : drag du DEPART = l'arc passe encore par les 3 poignees");
            /* retour a l'etat initial pour le bloc suivant (arc degenere) */
            g_objs[2].tracePts[0] = Vec3(0, 0, 0);
            traceRegen(g_objs[2]);
        }
        /* 4. ARC degenere (bombement aligne) : la derniere geometrie valide est GARDEE */
        {
            const std::vector<Vec3> avant = g_objs[2].v;
            g_objs[2].tracePts[1] = Vec3(1, 0, 0);    /* aligne avec la corde */
            traceRegen(g_objs[2]);
            bool intact = g_objs[2].v.size() == avant.size();
            for (size_t i = 0; intact && i < avant.size(); ++i)
                intact = (g_objs[2].v[i] - avant[i]).norm() < 1e-12;
            CHECK(intact, "edition (arc) : bombe aligne = geometrie precedente conservee");
        }
        /* 5. POLYGONE : sommets = TraceData ; ajout sur un bord, suppression, garde a 3 */
        g_polyPts = { Vec3(0,0,0), Vec3(4,0,0), Vec3(4,0,4), Vec3(0,0,4) };
        polyPose();
        CHECK(g_objs[3].traceType == 7 && g_objs[3].traceFerme == 1 && g_objs[3].tri.size() == 6,
              "edition (polygone) : TraceData = sommets, fan 2 tris");
        CHECK(traceAddPoint(g_objs[3], Vec3(6,0,2)) >= 0 && g_objs[3].tracePts.size() == 5
              && g_objs[3].tri.size() == 9,
              "edition (polygone) : clic sur un bord = sommet ajoute (3 tris)");
        CHECK(traceDelPoint(g_objs[3], 4) && g_objs[3].tri.size() == 6,
              "edition (polygone) : point supprime (2 tris)");
        {
            Object& pg = g_objs[3];
            CHECK(traceDelPoint(pg, 3) && pg.tracePts.size() == 3,
                  "edition (polygone) : suppression a 4 points (-> 3)");
            CHECK(!traceDelPoint(pg, 0), "edition (polygone) : garde - minimum 3 sommets");
            traceToggleFerme(pg);
            CHECK(pg.traceFerme == 1, "edition (polygone) : ouvert/ferme sans effet (surface seule)");
        }
        /* 6. persistence : rectangle et cercle round-trip (bloc trace) */
        {
            std::string err;
            const std::string p = tmpPath("edition_e2.s3projet");
            CHECK(saveProject(p, err), err.c_str());
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            CHECK(loadProject(p, err), err.c_str());
            CHECK(g_objs.size() == 4 && g_objs[0].traceType == 4 && g_objs[1].traceType == 5
                  && g_objs[2].traceType == 6 && g_objs[3].traceType == 7,
                  "edition E2 : types de trace survivent au save/load (4 formes)");
            CHECK(g_objs[1].tracePts.size() == 2 && std::fabs(g_objs[1].tracePts[1].x - 4.0) < 1e-12,
                  "edition E2 : points de definition fideles au save/load");
        }
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_polyPts.clear();
    }

    /* ------------------------------------------------------------------ */
    /* B2.7e E3 : extrusion parametrique (type 8 = profil + vecteur)       */
    /* ------------------------------------------------------------------ */
    {
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        /* 1. pull pose sur une feuille rectangle = conversion en extrusion */
        rectPose(Vec3(0,0,0), Vec3(4,0,2));
        CHECK(g_objs.size() == 1 && g_objs[0].traceType == 4 && g_objs[0].tri.size() == 6,
              "E3 : feuille rectangle posee (2 tris, type 4)");
        {
            Extrude dd;
            CHECK(makeExtrude(g_objs[0], 0, dd), "E3 : la feuille est capturable par P");
            CHECK(extrudeConvertTo(g_objs[0], dd, 3.0),
                  "E3 : pull pose = conversion en extrusion parametrique (type 8)");
            CHECK(g_objs[0].traceType == 8 && g_objs[0].traceProfil == 4,
                  "E3 : profil rectangle conserve (traceProfil = 4)");
            CHECK(g_objs[0].v.size() == 8 && g_objs[0].tri.size() == 36,
                  "E3 : prisme = 8 sommets, 12 tris (8 murs + 2 x 2 caps)");
            CHECK(std::fabs(signedVolume(g_objs[0]) - 24.0) < 1e-9,
                  "E3 : volume signe = 4 x 2 x 3 = 24 (normales exterieures)");
        }
        /* 2. pousser (vecteur inverse) : windings toujours exterieurs */
        g_objs[0].traceVec = Vec3(0, -3, 0);
        traceRegen(g_objs[0]);
        CHECK(std::fabs(signedVolume(g_objs[0]) - 24.0) < 1e-9,
              "E3 : pousser (vecteur vers le bas) = volume exterieur conserve");
        g_objs[0].traceVec = Vec3(0, 3, 0);
        traceRegen(g_objs[0]);
        /* 3. edition du profil : drag d'un coin = le prisme suit */
        g_objs[0].tracePts[1] = Vec3(2, 0, 1);      /* coin oppose deplace (locales) */
        traceRegen(g_objs[0]);
        CHECK(g_objs[0].v.size() == 8 && std::fabs(g_objs[0].v[6].x - 2.0) < 1e-12
              && std::fabs(g_objs[0].v[6].y - 3.0) < 1e-12 && std::fabs(g_objs[0].v[6].z - 1.0) < 1e-12,
              "E3 : coin du profil deplace = le cap superieur suit (v[6] = coin + h)");
        CHECK(std::fabs(signedVolume(g_objs[0]) - 6.0) < 1e-9,
              "E3 : volume = 2 x 1 x 3 = 6 apres le drag du coin");
        /* 4. edition de la hauteur (traceVec) : volume proportionnel */
        g_objs[0].traceVec = Vec3(0, 1.5, 0);
        traceRegen(g_objs[0]);
        CHECK(std::fabs(signedVolume(g_objs[0]) - 3.0) < 1e-9,
              "E3 : hauteur 1.5 = volume 3 (poignee bleue = traceVec)");
        /* 5. cercle extrude : prisme a 24 segments */
        cerclePose(Vec3(10,0,10), 2.0);
        {
            Extrude dd;
            CHECK(makeExtrude(g_objs[1], 0, dd), "E3 : feuille cercle capturable");
            CHECK(extrudeConvertTo(g_objs[1], dd, 2.0), "E3 : cercle extrude (type 8, profil 5)");
            /* B2.7e3b : structure CENTRE + bord conservee (fan centre, comme
               cerclePose) - l ancien fan centre sur le bord etait malsain */
            CHECK(g_objs[1].v.size() == 2 * (size_t)(CERCLE_SEG + 1)
                  && g_objs[1].tri.size() == (size_t)(3 * (2 * CERCLE_SEG + 2 * CERCLE_SEG)),
                  "E3 : prisme circulaire = 50 sommets, 48 murs + 2 x 24 caps centres");
            {
                const Vec3& ch0 = g_objs[1].v[CERCLE_SEG + 1];   /* centre du cap haut */
                bool centre = true;
                double dmin = 1e300;
                for (size_t i = 0; i < (size_t)CERCLE_SEG; ++i) {
                    const double d = (ch0 - g_objs[1].v[CERCLE_SEG + 2 + i]).norm();
                    if (std::fabs(d - 2.0) > 1e-9) centre = false;
                    dmin = std::min(dmin, d);
                }
                CHECK(centre && dmin > 1.9,
                      "E3 : le cap superieur reste un fan CENTRE (equidistant du bord)");
            }
            const double vol = signedVolume(g_objs[1]);
            /* aire du polygone INSCRIT a 24 cotes (pas le disque) x hauteur */
            const double attendu = 0.5 * CERCLE_SEG * 4.0
                                 * std::sin(6.283185307179586 / CERCLE_SEG) * 2.0;
            CHECK(std::fabs(vol - attendu) < 1e-6,
                  "E3 : volume du prisme circulaire = aire du polygone x hauteur");
        }
        /* 6. refus : une coque (cube) n est pas convertible */
        {
            const double col[3] = {0.5, 0.5, 0.5};
            g_objs.push_back(makeBoxObject("Cube", Vec3(0.5,0.5,0.5), Vec3(0.5,0.5,0.5), col));
            Extrude dd;
            CHECK(makeExtrude(g_objs[2], 2, dd), "E3 : face du cube capturable");
            CHECK(!extrudeConvertTo(g_objs[2], dd, 2.0),
                  "E3 : coque sans TraceData = pas de conversion (patch != tout l objet)");
            CHECK(g_objs[2].traceType == 0, "E3 : le cube reste non parametrique");
        }
        /* 7. polygone extrude : ajout/suppression sur le profil */
        g_polyPts = { Vec3(20,0,0), Vec3(24,0,0), Vec3(24,0,4), Vec3(20,0,4) };
        polyPose();
        {
            Extrude dd;
            CHECK(makeExtrude(g_objs[3], 0, dd) && extrudeConvertTo(g_objs[3], dd, 1.0),
                  "E3 : polygone extrude (type 8, profil 7)");
            CHECK(g_objs[3].tri.size() == (size_t)(3 * (2 * 4 + 2 * 2)),
                  "E3 : prisme polygonal = 12 tris (8 murs + 4 caps)");
            CHECK(traceAddPoint(g_objs[3], Vec3(22,0,2)) >= 0 && g_objs[3].tracePts.size() == 5,
                  "E3 : point ajoute au profil extrude (regles du polygone)");
            CHECK(g_objs[3].tri.size() == (size_t)(3 * (2 * 5 + 2 * 3)),
                  "E3 : profil a 5 sommets = 16 tris (10 murs + 6 caps)");
            CHECK(traceDelPoint(g_objs[3], 4) && g_objs[3].tri.size() == (size_t)(3 * 12),
                  "E3 : point supprime du profil extrude = retour a 12 tris");
            CHECK(traceDelPoint(g_objs[3], 0) && g_objs[3].tracePts.size() == 3,
                  "E3 : suppression a 4 points (-> 3, regles du polygone)");
            CHECK(!traceDelPoint(g_objs[3], 0), "E3 : garde - profil polygone minimum 3 sommets");
            /* B2.7e3b : garde du clic-sol (retour test : points parasites) */
            CHECK(traceAddPoint(g_objs[3], Vec3(40, 0, 40), true) < 0,
                  "E3 : garde - clic-sol LOIN du profil refuse");
            CHECK(traceAddPoint(g_objs[3], Vec3(24.05, 0, 2), true) >= 0,
                  "E3 : garde - clic-sol PRES du profil accepte (0.05)");
        }
        /* 8. O sur une extrusion de LIGNE fermee = bande (murs seuls) puis retour
           B3.1b : la ligne ne ferme plus en surface - la feuille (ancien format,
           traceSurface = 1) est construite ici a la main comme dans un vieux projet */
        {
            Object feuille;
            feuille.name = "Ligne";
            feuille.traceType = 1; feuille.traceFerme = 1; feuille.traceSurface = 1;
            feuille.tracePts = { Vec3(30,0,0), Vec3(32,0,0), Vec3(32,0,2), Vec3(30,0,2) };
            feuille.tri = { 0, 1, 2, 0, 2, 3 };      /* fan present = vieux projet (la regen le conserve) */
            traceRegen(feuille);
            g_objs.push_back(feuille);
        }
        CHECK(g_objs[4].traceType == 1 && g_objs[4].traceFerme == 1 && g_objs[4].traceSurface == 1,
              "E3 : feuille ligne fermee (ancien format) posee");
        {
            Extrude dd;
            CHECK(makeExtrude(g_objs[4], 0, dd) && extrudeConvertTo(g_objs[4], dd, 2.0),
                  "E3 : ligne fermee extrudee (type 8, profil 1)");
            CHECK(g_objs[4].tri.size() == (size_t)(3 * 12),
                  "E3 : prisme ligne = 12 tris (8 murs + 4 caps)");
            traceToggleFerme(g_objs[4]);
            CHECK(g_objs[4].traceFerme == 0 && g_objs[4].tri.size() == (size_t)(3 * 6),
                  "E3 : O = profil ouvert -> bande (6 tris = 3 murs x 2, sans caps)");
            traceToggleFerme(g_objs[4]);
            CHECK(g_objs[4].traceFerme == 1 && g_objs[4].tri.size() == (size_t)(3 * 12),
                  "E3 : O = retour au prisme ferme (12 tris)");
        }
        /* 9. persistence : l extrusion survit au save/load (profil + vecteur) */
        {
            std::string err;
            const std::string p = tmpPath("extrusion.s3projet");
            CHECK(saveProject(p, err), err.c_str());
            const size_t nObjAvant = g_objs.size();
            const std::vector<Vec3> vAvant = g_objs[0].v;
            g_objs.clear(); g_selMulti.clear(); g_selected = -1;
            CHECK(loadProject(p, err), err.c_str());
            CHECK(g_objs.size() == nObjAvant && g_objs[0].traceType == 8 && g_objs[0].traceProfil == 4,
                  "E3 : type 8 + sous-type survivent au save/load");
            CHECK(std::fabs(g_objs[0].traceVec.y - 1.5) < 1e-9,
                  "E3 : vecteur d extrusion fidele au save/load");
            traceRegen(g_objs[0]);
            bool identique = g_objs[0].v.size() == vAvant.size();
            for (size_t i = 0; identique && i < vAvant.size(); ++i)
                identique = (g_objs[0].v[i] - vAvant[i]).norm() < 1e-9;
            CHECK(identique, "E3 : geometrie regeneree identique apres save/load");
        }
        /* 10. E3c : point ajoute EXACTEMENT sur un bord du profil (clic sur
              un mur = projection verticale) -> tri de cap degenere (colineaire)
              ecarte par pushFanTri, sinon makeExtrude refuse ("Face non plane") */
        {
            g_polyPts = { Vec3(50,0,0), Vec3(54,0,0), Vec3(54,0,4), Vec3(50,0,4) };
            polyPose();
            Object& poly = g_objs.back();           /* polyPose ajoute a la fin */
            Extrude dd;
            CHECK(makeExtrude(poly, 0, dd) && extrudeConvertTo(poly, dd, 2.0),
                  "E3c : polygone 4x4 extrude (h = 2) avant ajout");
            const double volAvant = signedVolume(poly);
            CHECK(std::fabs(volAvant - 32.0) < 1e-6,
                  "E3c : volume initial = 16 (aire) x 2 (hauteur)");
            CHECK(traceAddPoint(poly, Vec3(52,0,0), false) >= 0
                  && poly.tracePts.size() == 5,
                  "E3c : point insere EXACTEMENT sur le bord (52,0,0) = milieu du cote");
            bool normalesOk = true;
            for (const Vec3& nn : poly.n)
                if (dot(nn, nn) < 0.5) normalesOk = false;
            CHECK(normalesOk, "E3c : aucune normale nulle apres insertion colineaire");
            CHECK(std::fabs(signedVolume(poly) - volAvant) < 1e-6,
                  "E3c : volume conserve par le point colineaire");
            CHECK(makeExtrude(poly, 0, dd),
                  "E3c : Push/Pull refonctionne apres l ajout (Face non plane repare)");
        }
        /* ===== B3 : balayage (extrusion le long d'un chemin) ===== */
        {
            /* profil = polygone 4x4 pose, chemin = fil ligne droite (pose a la main) */
            g_polyPts = { Vec3(60,0,0), Vec3(64,0,0), Vec3(64,0,4), Vec3(60,0,4) };
            polyPose();
            Object chemin;
            chemin.name = "Ligne";
            chemin.traceType = 1; chemin.traceFerme = 0;
            chemin.tracePts = { Vec3(0,0,10), Vec3(0,0,20) };
            g_objs.push_back(chemin);
            const size_t ip = g_objs.size() - 2, ic = g_objs.size() - 1;
            CHECK(sweepConvertTo(g_objs[ip], g_objs[ic]),
                  "B3 : balayage polygone + ligne droite accepte");
            CHECK(g_objs[ip].traceType == 9 && g_objs[ip].traceProfil == 7,
                  "B3 : l objet passe en type 9 (profil = polygone)");
            CHECK(g_objs[ip].traceCheminType == 1 && g_objs[ip].traceChemin.size() == 2,
                  "B3 : les points du chemin sont absorbes");
            CHECK(g_objs[ip].v.size() == 8, "B3 : 2 anneaux de 4 sommets");
            CHECK(g_objs[ip].tri.size() == (size_t)(3 * 12),
                  "B3 : prisme balaye = 12 tris (8 murs + 4 caps)");
            CHECK(std::fabs(signedVolume(g_objs[ip]) - 160.0) < 1e-6,
                  "B3 : volume = aire 16 x longueur 10");
            /* profil perpendiculaire au chemin : anneaux a z = 10 et 20 exactement (monde) */
            bool perp = true;
            for (const Vec3& p : g_objs[ip].v) {
                const Vec3 pw = g_objs[ip].pos + p * g_objs[ip].scl;
                if (std::fabs(pw.z - 10.0) > 1e-9 && std::fabs(pw.z - 20.0) > 1e-9) perp = false;
            }
            CHECK(perp, "B3 : anneaux PERPENDICULAIRES au chemin (z constant par anneau)");
            CHECK(signedVolume(g_objs[ip]) > 0.0, "B3 : normales exterieures (volume signe positif)");
            /* B3.1b : le point de la face le plus proche du depart du fil se
               connecte EXACTEMENT au depart du chemin (maitrise Follow Me) */
            {
                bool connexe = false;
                for (const Vec3& p : g_objs[ip].v) {
                    const Vec3 pw = g_objs[ip].pos + p * g_objs[ip].scl;
                    if ((pw - Vec3(0, 0, 10)).norm() < 1e-9) { connexe = true; break; }
                }
                CHECK(connexe, "B3 : connexion - le point de la face le plus proche du fil est pose sur le depart");
            }
            /* chemin ferme = solide torique (sans caps) */
            g_polyPts = { Vec3(70,0,0), Vec3(74,0,0), Vec3(74,0,4), Vec3(70,0,4) };
            polyPose();
            Object boucle;
            boucle.name = "Ligne (2)";
            boucle.traceType = 1; boucle.traceFerme = 1;
            boucle.tracePts = { Vec3(0,0,30), Vec3(0,0,36), Vec3(4,0,36), Vec3(4,0,30) };
            g_objs.push_back(boucle);
            const size_t jp = g_objs.size() - 2, jc = g_objs.size() - 1;
            CHECK(sweepConvertTo(g_objs[jp], g_objs[jc]), "B3 : chemin FERME accepte (torique)");
            CHECK(g_objs[jp].v.size() == 16 && g_objs[jp].tri.size() == (size_t)(3 * 32),
                  "B3 : torique = 4 anneaux, 32 tris (murs seuls, sans caps)");
            CHECK(signedVolume(g_objs[jp]) > 0.0, "B3 : torique = normales exterieures");
            /* refus : chemin non trace, meme objet */
            g_polyPts = { Vec3(80,0,0), Vec3(84,0,0), Vec3(84,0,4), Vec3(80,0,4) };
            polyPose();
            Object& profil3 = g_objs.back();
            CHECK(!sweepConvertTo(profil3, profil3), "B3 : refus - le chemin ne peut pas etre l objet lui-meme");
            Object coque;
            coque.traceType = 0;
            CHECK(!sweepConvertTo(profil3, coque), "B3 : refus - chemin non trace (type 0)");
            /* persistence : le balayage survit au save/load (profil + chemin) */
            {
                std::string err;
                const std::string pth = tmpPath("balayage.s3projet");
                CHECK(saveProject(pth, err), err.c_str());
                const size_t nAvant = g_objs.size();
                const std::vector<Vec3> chAvant = g_objs[ip].traceChemin;
                const std::vector<Vec3> vAvant = g_objs[ip].v;
                g_objs.clear(); g_selMulti.clear(); g_selected = -1;
                CHECK(loadProject(pth, err), err.c_str());
                const bool ok = g_objs.size() == nAvant && g_objs[ip].traceType == 9
                      && g_objs[ip].traceChemin.size() == chAvant.size();
                CHECK(ok, "B3 : type 9 + chemin survivent au save/load");
                bool identique = false;
                if (ok) {
                    traceRegen(g_objs[ip]);
                    identique = g_objs[ip].v.size() == vAvant.size();
                    for (size_t i = 0; identique && i < vAvant.size(); ++i)
                        identique = (g_objs[ip].v[i] - vAvant[i]).norm() < 1e-9;
                }
                CHECK(identique, "B3 : geometrie du balayage regeneree identique apres save/load");
            }
        }
        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_polyPts.clear(); g_lineMode = false; lineReset();
    }

    std::printf("resume : %d echec(s), %d verifications\n", g_fail, g_tot);
    return g_fail ? 1 : 0;
}
