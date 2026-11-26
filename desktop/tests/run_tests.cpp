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
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("ECHEC: %s\n", (msg)); ++g_fail; } \
                              else { std::printf("ok : %s\n", (msg)); } } while (0)

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
        g_lineFerme = true;                          /* defaut : surface fermee */
        g_linePts = { Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 2), Vec3(0, 0, 2) };
        g_lineActive = true;
        g_lineMode = true;
        lineCloseLoop();
        CHECK(g_objs.size() == 1 && g_objs[0].tri.size() == 6 && g_objs[0].edges.size() == 5,
              "ligne : fermeture en surface = feuille FUSEE au fil (2 tris) ; aretes derivees = 4 bords + 1 diagonale du fan (v1)");
        CHECK(std::fabs(signedVolume(g_objs[0])) < 1e-12 && g_objs[0].v.size() == 4,
              "ligne : feuille fermee = 4 sommets (pas de doublon du depart), volume plat nul");
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

        g_objs.clear(); g_selMulti.clear(); g_selected = -1;
        g_lineMode = false;
        lineReset();
    }

    std::printf("resume : %d echec(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
