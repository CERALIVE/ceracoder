# Ceracoder Versioning

This document describes the versioning scheme used for Ceracoder releases.

## Overview

Ceracoder uses **Calendar Versioning (CalVer)** with automatic version detection, matching the versioning scheme used in [CeraUI](https://github.com/CERALIVE/CeraUI).

## Version Format

| Release Type | Format | Example |
|--------------|--------|---------|
| **Stable** | `YYYY.M.patch` | `v2026.1.0`, `v2026.1.1`, `v2026.2.0` |
| **Beta** | `YYYY.M.patch-beta.N` | `v2026.1.1-beta.1`, `v2026.1.2-beta.2` |

Where:
- `YYYY` = 4-digit year (e.g., 2026)
- `M` = Month without leading zero (e.g., 1 for January, 12 for December)
- `patch` = Incrementing patch number starting from 0 each month
- `N` = Beta number, incremented for each beta release

## Version Calculation

Versions are automatically calculated based on existing git tags. The system looks at the current year and month, then determines the next appropriate version number.

### Stable Release Examples

```
January 2026:
  First stable release:     v2026.1.0
  Second stable release:    v2026.1.1
  Third stable release:     v2026.1.2

February 2026 (new month, patch resets):
  First stable release:     v2026.2.0
  Second stable release:    v2026.2.1
```

### Beta Release Examples

Beta releases use the **next** patch number with a beta suffix:

```
January 2026:
  Current stable:           v2026.1.1
  First beta (next patch):  v2026.1.2-beta.1
  Second beta:              v2026.1.2-beta.2
  Third beta:               v2026.1.2-beta.3
  
  When v2026.1.2 is released as stable:
  Next beta:                v2026.1.3-beta.1
```

### Key Rules

1. **Stable releases** increment the patch number for the current year.month
2. **Beta releases** use the next patch number with a `-beta.N` suffix
3. Multiple betas can exist for the same patch version (beta.1, beta.2, etc.)
4. When a new month starts, the patch number resets to 0
5. Beta numbers are tracked independently per patch version

## Creating Releases

### Using GitHub Actions (Recommended)

Releases are created through GitHub Actions workflow dispatch:

1. Navigate to **Actions** → **Build and Release Ceracoder**
2. Click **Run workflow**
3. Select parameters:
   - **Release type**: `stable` or `beta`
   - **Release notes**: Optional description of changes
   - **Force version**: Optional manual version override (e.g., `2026.1.5`)
4. Click **Run workflow**

The workflow will:
- Automatically calculate the next version based on existing tags
- Build binaries for x86_64 and ARM64
- Create a GitHub release with all assets
- Tag the release with the calculated version
- Mark beta releases as "pre-release"

### Manual Versioning

If you need to override the automatic version detection, use the `force_version` input:

```
force_version: 2026.1.5
```

This is useful for:
- Hotfix releases out of sequence
- Correcting version numbering issues
- Creating specific version numbers for compatibility

## Version Information in Binary

The version is compiled into the binary via the `VERSION` macro defined in the Makefile:

```makefile
VERSION=$(shell git rev-parse --short HEAD)
CFLAGS=... -DVERSION=\"$(VERSION)\" ...
```

To display the version:

```bash
ceracoder -v
```

## Release Workflow Details

### Workflow Inputs

| Input | Required | Description | Options |
|-------|----------|-------------|---------|
| `release_type` | Yes | Type of release | `stable`, `beta` |
| `release_notes` | No | Description of changes | Free text |
| `force_version` | No | Override auto version | e.g., `2026.1.0` |

### Workflow Jobs

1. **calculate-version**
   - Fetches all git tags with `fetch-depth: 0`
   - Determines next version based on `release_type`
   - Outputs: `version`, `version_tag`, `is_beta`

2. **build-release** (matrix: x86_64, arm64)
   - Builds ceracoder binary for each architecture
   - Creates compressed `.tar.gz` archives
   - Generates SHA256 checksums
   - Uploads artifacts

3. **create-release**
   - Downloads all build artifacts
   - Generates changelog from commit history
   - Creates GitHub release with:
     - Version tag (e.g., `v2026.1.0`)
     - Release name with beta indicator if applicable
     - Formatted release notes
     - All binary archives and checksums
     - Pre-release flag for beta releases

## Debian Package Versioning

Debian packages use the same CalVer versioning scheme. The `publish-deb.yml` workflow:

- Automatically calculates the CalVer version
- Builds ARM64 Debian packages
- Packages are named: `ceracoder_YYYY.M.patch_arm64.deb` or `ceracoder_YYYY.M.patch-beta.N_arm64.deb`

To trigger a Debian package build:

```bash
# Via GitHub Actions
Actions → Build and Upload Debian Package → Run workflow

# Or automatically on push to master (builds beta by default)
git push origin master
```

## Migration from Previous Versioning

Ceracoder previously used git commit hashes as versions. The new CalVer system:

- Provides human-readable version numbers
- Enables proper version ordering and comparison
- Aligns with CeraUI's versioning scheme
- Supports both stable and beta release channels
- Integrates with Debian package management

Existing installations using commit-based versions will continue to work. New releases will use the CalVer format.

## Best Practices

### When to Use Beta Releases

Use beta releases for:
- Testing new features before stable release
- Gathering user feedback
- Pre-release validation
- Release candidates

### When to Use Stable Releases

Use stable releases for:
- Production deployments
- Official announcements
- Long-term support versions
- Recommended installations

### Release Frequency

- **Stable**: Release when features are complete and tested
- **Beta**: Release as often as needed for testing
- **Monthly**: Patch number resets each month, creating natural release cycles

## Troubleshooting

### Version Calculation Errors

If the version calculation fails:

1. Ensure you have git tags fetched: `git fetch --tags`
2. Check existing tags: `git tag -l`
3. Use `force_version` input to override if needed

### Wrong Version Detected

If the wrong version is detected:

1. Verify your git tags follow the format `vYYYY.M.patch` or `vYYYY.M.patch-beta.N`
2. Check that old tags don't interfere (e.g., `v1.0.0` from old versioning)
3. Use `force_version` to specify the correct version

### Beta Version Conflicts

If beta versioning is incorrect:

1. List beta tags: `git tag -l "v*-beta.*"`
2. Ensure beta tags follow the exact format `vYYYY.M.patch-beta.N`
3. Remove conflicting tags if necessary

## Related Documentation

- [CeraUI Versioning](https://github.com/CERALIVE/CeraUI/blob/master/docs/BUILD_PIPELINE.md) - Reference implementation
- [GitHub Actions Workflows](../.github/workflows/) - Workflow definitions
- [Makefile](../Makefile) - Build configuration with VERSION macro

## Examples

### Creating a Stable Release

```bash
# 1. Go to GitHub Actions → Build and Release Ceracoder
# 2. Select:
#    - release_type: stable
#    - release_notes: "Added SRT statistics logging"
# 3. Run workflow
# 
# Result: v2026.1.0 (or next available version)
```

### Creating a Beta Release

```bash
# 1. Go to GitHub Actions → Build and Release Ceracoder
# 2. Select:
#    - release_type: beta
#    - release_notes: "Testing new bitrate algorithm"
# 3. Run workflow
#
# Result: v2026.1.1-beta.1 (or next available beta)
```

### Force a Specific Version

```bash
# 1. Go to GitHub Actions → Build and Release Ceracoder
# 2. Select:
#    - release_type: stable
#    - release_notes: "Hotfix for critical bug"
#    - force_version: 2026.1.5
# 3. Run workflow
#
# Result: v2026.1.5 (regardless of existing versions)
```

---

For questions or issues with versioning, please open an issue on [GitHub](https://github.com/CERALIVE/ceracoder/issues).
