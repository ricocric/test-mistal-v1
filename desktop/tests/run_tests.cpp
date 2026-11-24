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
        g_rotPivot = centre;
        applyRotationAt(o2, base2, 2, 90.0);   /* Z de +90 deg */
        const double dx = o2.bmax.x - o2.bmin.x, dy = o2.bmax.y - o2.bmin.y, dz = o2.bmax.z - o2.bmin.z;
        const Vec3 centre2 = o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl);
        CHECK(std::fabs(dx - 1.2) < 1e-9 && std::fabs(dy - 3.0) < 1e-9 && std::fabs(dz - 1.6) < 1e-9,
              "rotation : +90 deg en Z permute les portees X/Y (pave debout)");
        CHECK((centre2 - centre).norm() < 1e-9, "rotation : le centre de la piece ne bouge pas (pivot)");
        CHECK(std::fabs(signedVolume(o2) - volAvant) < 1e-9 && countNonManifold(o2) == 0,
              "rotation : volume preserve, aretes toujours coherentes");
        const std::vector<Vec3> at90 = o2.v;
        applyRotationAt(o2, at90, 2, -90.0);
        bool vRet = o2.v.size() == base2.size();
        for (size_t i = 0; vRet && i < base2.size(); ++i) vRet = (o2.v[i] - base2[i]).norm() < 1e-12;
        CHECK(vRet, "rotation : +90 puis -90 -> geometrie d'origine a 1e-12 pres");
        applyRotationAt(o2, base2, 0, 180.0);   /* X de 180 deg */
        bool v180 = o2.v.size() == base2.size();
        for (size_t i = 0; v180 && i < base2.size(); ++i)
            v180 = std::fabs(o2.v[i].x - base2[i].x) < 1e-12
                && std::fabs(o2.v[i].y + base2[i].y) < 1e-12
                && std::fabs(o2.v[i].z + base2[i].z) < 1e-12;
        CHECK(v180 && (o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl) - centre).norm() < 1e-9,
              "rotation : 180 deg en X -> miroir Y/Z, pivot fixe");
        /* changement d'axe en chaine (lettre pendant une rotation) : +90 Z pose,
           puis +90 X sur la geometrie courante avec pivot recadre */
        applyRotationAt(o2, base2, 2, 90.0);
        const std::vector<Vec3> z90 = o2.v;
        g_rotPivot = o2.pos + (o2.bmin + o2.bmax) * (0.5 * o2.scl);
        applyRotationAt(o2, z90, 0, 90.0);
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
        exportOBJ(tmpPath("objet_seul.obj"), g_selected);
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
        exportSTL(tmpPath("objet_seul.stl"), 0);
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

    std::printf("resume : %d echec(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
