#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Core/guid.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Engine;

namespace {
    std::filesystem::path MakeTempDir(const std::string &name) {
        const auto dir = std::filesystem::temp_directory_path() / ("asset_path_test_" + name);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        ec.clear();
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
} // namespace

int main() {
    // 5.1 AssetPath value semantics
    {
        AssetPath empty;
        assert(empty.IsEmpty());

        AssetPath a1("res://materials/wood.asset");
        assert(!a1.IsEmpty());
        assert(a1.GetScheme() == "res");
        assert(a1.GetSubPath().generic_string() == "materials/wood.asset");
        assert(a1.ToString() == "res://materials/wood.asset");

        // Scheme case is normalized to lowercase.
        AssetPath a2("RES://materials/wood.asset");
        assert(a1 == a2);
        assert(AssetPath::Hash{}(a1) == AssetPath::Hash{}(a2));

        // Dot segments are folded.
        AssetPath folded("res://materials/../textures/./wood.asset");
        assert(folded.ToString() == "res://textures/wood.asset");

        // Escaping the root is clamped.
        AssetPath clamped("res://../../etc");
        assert(clamped.ToString() == "res://etc");

        // The root path returns itself as parent and has no filename.
        AssetPath root("res://");
        assert(root.parent_path() == root);
        assert(root.filename().empty());

        AssetPath nested("res://a/b");
        assert(nested.parent_path().ToString() == "res://a");
        assert(nested.filename() == "b");

        // Appending re-normalizes.
        AssetPath appended = AssetPath("res://a") / "b/c.asset";
        assert(appended.ToString() == "res://a/b/c.asset");
    }

    // 5.2 Scheme mount resolution
    {
        const auto res_dir = MakeTempDir("res");
        const auto nested_builtin = res_dir / "engine";
        std::filesystem::create_directories(nested_builtin);

        FileSystemDatabase db;
        db.RegisterScheme(AssetPath::k_scheme_res, res_dir, true);
        db.RegisterScheme(AssetPath::k_scheme_builtin, nested_builtin, false);

        // ToAbsolutePath resolves per scheme.
        assert(db.ToAbsolutePath(AssetPath("res://meshes/cube.asset")).generic_string()
               == (res_dir / "meshes/cube.asset").generic_string());

        // Unmounted scheme throws.
        bool threw = false;
        try {
            static_cast<void>(db.ToAbsolutePath(AssetPath("pak://data/x")));
        } catch (const std::runtime_error &) {
            threw = true;
        }
        assert(threw);

        // Longest prefix wins on reverse mapping.
        const AssetPath nested_ap = db.FromAbsolutePath(nested_builtin / "mesh/cube.asset");
        assert(nested_ap.ToString() == "builtin://mesh/cube.asset");

        // Read-only builtin mount rejects writes.
        assert(!db.CreateDirectory(AssetPath("builtin://some/dir")));
        // Writable res mount accepts writes.
        assert(db.CreateDirectory(AssetPath("res://new/dir")));

        // Custom scheme registration.
        db.RegisterScheme(AssetPath::k_scheme_usr, res_dir / "usr", true);
        assert(db.ToAbsolutePath(AssetPath("usr://settings.cfg")).generic_string()
               == (res_dir / "usr/settings.cfg").generic_string());

        std::error_code ec;
        std::filesystem::remove_all(res_dir, ec);
    }

    // 5.3 Interface path index
    {
        const auto res_dir = MakeTempDir("index");
        std::ofstream(res_dir / "a.asset") << "{}";

        FileSystemDatabase db;
        db.RegisterScheme(AssetPath::k_scheme_res, res_dir, true);

        const GUID guid = GUID::Random();
        db.AddAsset(guid, AssetPath("res://a.asset"));

        // GetGUID returns the registered guid.
        const auto found = db.GetGUID(AssetPath("res://a.asset"));
        assert(found.has_value() && *found == guid);

        // Unknown path returns nullopt.
        assert(!db.GetGUID(AssetPath("res://missing.asset")).has_value());

        // GetNewAssetRef returns an unloaded ref with the guid.
        const AssetRef ref = db.GetNewAssetRef(AssetPath("res://a.asset"));
        assert(ref.GetGUID() == guid);

        // Unknown path throws from GetNewAssetRef.
        bool threw = false;
        try {
            static_cast<void>(db.GetNewAssetRef(AssetPath("res://missing.asset")));
        } catch (const std::runtime_error &) {
            threw = true;
        }
        assert(threw);

        // MovePath preserves the GUID while remapping the path.
        assert(db.MovePath(AssetPath("res://a.asset"), AssetPath("res://b.asset")));
        assert(db.GetAssetPath(guid) == AssetPath("res://b.asset"));
        assert(!std::filesystem::exists(res_dir / "a.asset"));
        assert(std::filesystem::exists(res_dir / "b.asset"));

        std::error_code ec;
        std::filesystem::remove_all(res_dir, ec);
    }

    return 0;
}
