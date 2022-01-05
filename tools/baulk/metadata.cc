// load package metadata
#include <baulk/json_utils.hpp>
#include <baulk/vfs.hpp>
#include <bela/fnmatch.hpp>
#include <bela/ascii.hpp>
#include <baulk/fs.hpp>
#include "bucket.hpp"

namespace baulk {

inline auto PackageMetaJoinNative(const Bucket &bucket, std::wstring_view pkgName) {
  return bela::StringCat(vfs::AppBuckets(), L"\\", bucket.name, L"\\bucket\\", pkgName, L".json");
}

std::optional<baulk::Package> PackageMetaNative(const Bucket &bucket, std::wstring_view pkgName, bela::error_code &ec) {
  auto pkgMeta = PackageMetaJoinNative(bucket, pkgName);
  auto pkj = baulk::json::parse_file(pkgMeta, ec);
  if (!pkj) {
    return std::nullopt;
  }
  auto jv = pkj->view();
  Package pkg{
      .name = std::wstring{pkgName},
      .description = jv.fetch("description"),
      .version = jv.fetch("version"),
      .bucket = std::wstring{bucket.name},
      .extension = bela::AsciiStrToLower(jv.fetch("extension")), // to lower
      .rename = jv.fetch("rename"),
      .homepage = jv.fetch("homepage"),
      .notes = jv.fetch("notes"),
      .license = jv.fetch("license"),
  };
  jv.fetch_strings_checked("suggest", pkg.suggest);
  jv.fetch_paths_checked("force_delete", pkg.forceDeletes);

#if defined(_M_X64)
  // x64
  if (jv.fetch_strings_checked("url64", pkg.urls)) {
    pkg.hashValue = jv.fetch("url64.hash");
  } else if (jv.fetch_strings_checked("url", pkg.urls)) {
    pkg.hashValue = jv.fetch("url.hash");
  } else {
    ec = bela::make_error_code(bela::ErrGeneral, pkgMeta, L" not yet port to x64 platform.");
    return std::nullopt;
  }
  if (!jv.fetch_paths_checked("links64", pkg.links)) {
    jv.fetch_paths_checked("links", pkg.links);
  }
  if (!jv.fetch_paths_checked("launchers64", pkg.launchers)) {
    jv.fetch_paths_checked("launchers", pkg.launchers);
  }
#elif defined(_M_ARM64)
  // ARM64 support
  if (jv.fetch_strings_checked("urlarm64", pkg.urls)) {
    pkg.hashValue = jv.fetch("urlarm64.hash");
  } else if (jv.fetch_strings_checked("url", pkg.urls)) {
    pkg.hashValue = jv.fetch("url.hash");
  } else if (jv.fetch_strings_checked("url64", pkg.urls)) {
    pkg.hashValue = jv.fetch("url64.hash");
  } else {
    ec = bela::make_error_code(bela::ErrGeneral, pkgMeta, L" not yet port to ARM64 platform.");
    return std::nullopt;
  }
  if (!jv.fetch_paths_checked("linksarm64", pkg.links)) {
    if (!jv.fetch_paths_checked("links", pkg.links)) {
      jv.fetch_paths_checked("links64", pkg.links);
    }
  }
  if (!jv.fetch_paths_checked("launchersarm64", pkg.launchers)) {
    if (!jv.fetch_paths_checked("launchers", pkg.launchers)) {
      jv.fetch_paths_checked("launchers", pkg.links);
    }
  }
#else
  if (jv.fetch_strings_checked("url", pkg.urls)) {
    pkg.hashValue = jv.fetch("url.hash");
  } else {
    ec = bela::make_error_code(bela::ErrGeneral, pkgMeta, L" not yet ported.");
    return std::nullopt;
  }
  jv.fetch_paths_checked("links", pkg.links);
  jv.fetch_paths_checked("launchers", pkg.launchers);
#endif
  if (auto sv = jv.subview("venv"); sv) {
    DbgPrint(L"pkg '%s' support virtual env\n", pkg.name);
    pkg.venv.category = sv->fetch("category");
    sv->fetch_paths_checked("path", pkg.venv.paths);
    sv->fetch_paths_checked("include", pkg.venv.includes);
    sv->fetch_paths_checked("lib", pkg.venv.libs);
    sv->fetch_paths_checked("mkdir", pkg.venv.mkdirs);
    sv->fetch_strings_checked("env", pkg.venv.envs);
    sv->fetch_strings_checked("dependencies", pkg.venv.dependencies);
  }
  return std::make_optional(std::move(pkg));
}

inline auto PackageMetaJoinScoop(const Bucket &bucket, std::wstring_view pkgName) {
  return bela::StringCat(vfs::AppBuckets(), L"\\", bucket.name, L"\\bucket\\", pkgName, L".json");
}

std::optional<baulk::Package> PackageMetaScoop(const Bucket &bucket, std::wstring_view pkgName, bela::error_code &ec) {

  return std::nullopt;
}

std::optional<baulk::Package> PackageMeta(const Bucket &bucket, std::wstring_view pkgName, bela::error_code &ec) {
  switch (bucket.variant) {
  case BucketVariant::Native:
    return PackageMetaNative(bucket, pkgName, ec);
  case BucketVariant::Scoop:
    return PackageMetaScoop(bucket, pkgName, ec);
  default:
    break;
  }
  ec = bela::make_error_code(bela::ErrUnimplemented, L"bucket not support variant: ", static_cast<int>(bucket.variant));
  return std::nullopt;
}

bool PackageMatchedInternal(const Bucket &bucket, std::wstring_view pkgMetaFolder, const OnPattern &op,
                            const OnMatched &om) {
  DbgPrint(L"search bucket: %s, metadata folder: %s", bucket.name, pkgMetaFolder);
  bela::fs::Finder finder;
  bela::error_code ec;
  if (!finder.First(pkgMetaFolder, L"*.json", ec)) {
    return false;
  }
  do {
    if (finder.Ignore()) {
      continue;
    }
    auto pkgName = finder.Name();
    pkgName.remove_suffix(5);
    if (!op(pkgName)) {
      continue;
    }
    om(bucket, pkgName);
  } while (finder.Next());
  return true;
}

bool PackageMatched(const OnPattern &op, const OnMatched &om) {
  for (const auto &bucket : LoadedBuckets()) {
    switch (bucket.variant) {
    case BucketVariant::Native: {
      auto pkgMetaFolder = bela::StringCat(vfs::AppBuckets(), L"\\", bucket.name, L"\\bucket\\");
      PackageMatchedInternal(bucket, pkgMetaFolder, op, om);
    } break;
    case BucketVariant::Scoop: {
      auto pkgMetaFolder = bela::StringCat(vfs::AppBuckets(), L"\\", bucket.name, L"\\bucket\\");
      PackageMatchedInternal(bucket, pkgMetaFolder, op, om);
    } break;
    default:
      break;
    }
  }
  return true;
}

} // namespace baulk