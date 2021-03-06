// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/mojo_url_resolver.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "mojo/shell/filename_util.h"
#include "url/url_util.h"

namespace mojo {
namespace shell {
namespace {

GURL AddTrailingSlashIfNeeded(const GURL& url) {
  if (!url.has_path() || *url.path().rbegin() == '/')
    return url;

  std::string path(url.path() + '/');
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  return url.ReplaceComponents(replacements);
}

}  // namespace

MojoURLResolver::MojoURLResolver() {
  // Needed to treat first component of mojo URLs as host, not path.
  url::AddStandardScheme("mojo");

  // By default, resolve mojo URLs to files living alongside the shell.
  base::FilePath path;
  PathService::Get(base::DIR_MODULE, &path);
  default_base_url_ = AddTrailingSlashIfNeeded(FilePathToFileURL(path));
}

MojoURLResolver::~MojoURLResolver() {
}

void MojoURLResolver::SetBaseURL(const GURL& base_url) {
  DCHECK(base_url.is_valid());
  // Force a trailing slash on the base_url to simplify resolving
  // relative files and URLs below.
  base_url_ = AddTrailingSlashIfNeeded(base_url);
}

void MojoURLResolver::AddCustomMapping(const GURL& mojo_url,
                                       const GURL& resolved_url) {
  url_map_[mojo_url] = resolved_url;
}

void MojoURLResolver::AddLocalFileMapping(const GURL& mojo_url) {
  local_file_set_.insert(mojo_url);
}

GURL MojoURLResolver::Resolve(const GURL& mojo_url) const {
  const GURL mapped_url(ApplyCustomMappings(mojo_url));

  // Continue resolving if the mapped url is a mojo: url.
  if (mapped_url.scheme() != "mojo")
    return mapped_url;

  std::string lib = mapped_url.host() + ".mojo";

  if (!base_url_.is_valid() ||
      local_file_set_.find(mapped_url) != local_file_set_.end()) {
    // Resolve to a local file URL.
    return default_base_url_.Resolve(lib);
  }

  // Otherwise, resolve to an URL relative to base_url_.
  return base_url_.Resolve(lib);
}

GURL MojoURLResolver::ApplyCustomMappings(const GURL& url) const {
  GURL mapped_url(url);
  for (;;) {
    std::map<GURL, GURL>::const_iterator it = url_map_.find(mapped_url);
    if (it == url_map_.end())
      break;
    mapped_url = it->second;
  }
  return mapped_url;
}

}  // namespace shell
}  // namespace mojo
