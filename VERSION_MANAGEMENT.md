# Version Management Guide for Staticwall

This guide explains how to properly version Staticwall releases for AUR (Arch User Repository) and other package managers.

## Current Versioning System

Staticwall uses **Semantic Versioning** (SemVer):
- Format: `MAJOR.MINOR.PATCH`
- Example: `0.2.0`

### Version Number Meanings

- **MAJOR** (0): Incompatible API changes
- **MINOR** (2): New features, backwards compatible
- **PATCH** (0): Bug fixes, backwards compatible

## Git Tagging for Releases

### Creating a New Release

1. **Update version in code** (if applicable):
   ```bash
   # Update Makefile or version constant
   vim Makefile  # Change VERSION := 0.2.0
   ```

2. **Commit all changes**:
   ```bash
   git add -A
   git commit -m "chore: bump version to 0.3.0"
   git push
   ```

3. **Create an annotated tag**:
   ```bash
   git tag -a v0.3.0 -m "Release 0.3.0 - Feature description"
   ```

4. **Push the tag to GitHub**:
   ```bash
   git push origin v0.3.0
   ```

5. **Create GitHub Release** (optional but recommended):
   - Go to: https://github.com/1ay1/staticwall/releases/new
   - Select the tag: `v0.3.0`
   - Add release notes
   - Publish release

## AUR Package Versioning

### How AUR Gets Version Information

The AUR PKGBUILD uses the `pkgver()` function to automatically determine version:

```bash
pkgver() {
    cd "$srcdir/staticwall"
    git describe --long --tags | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
}
```

This generates versions like:
- `0.2.0` - Exact tag match
- `0.2.0.r5.gabc1234` - 5 commits after tag v0.2.0

### Version Format Explanation

Format: `TAG.rREVISIONS.gCOMMIT_HASH`
- `TAG`: Last git tag (e.g., `0.2.0`)
- `r`: Indicates revision count
- `REVISIONS`: Number of commits since tag
- `g`: Git prefix
- `COMMIT_HASH`: Short git hash

Examples:
- `0.2.0.r0.gcf99dac` - Same as v0.2.0 tag
- `0.2.0.r12.g1a2b3c4` - 12 commits after v0.2.0

## Release Workflow

### For Minor/Major Releases

1. **Finish all features for the release**
2. **Test thoroughly**
3. **Update documentation** (README, CHANGELOG)
4. **Update version references**:
   - `Makefile` (VERSION variable)
   - Any version constants in code
5. **Commit and tag**:
   ```bash
   git add -A
   git commit -m "chore: release v0.3.0"
   git tag -a v0.3.0 -m "Release 0.3.0

   New features:
   - Feature A
   - Feature B
   
   Bug fixes:
   - Fix C
   - Fix D"
   git push origin main v0.3.0
   ```

### For Hotfix Releases (Patch)

1. **Create hotfix from main**:
   ```bash
   git checkout main
   git pull
   ```

2. **Fix the bug**:
   ```bash
   # Make your fixes
   git add -A
   git commit -m "fix: critical bug in X"
   ```

3. **Tag patch version**:
   ```bash
   git tag -a v0.2.1 -m "Hotfix 0.2.1 - Fix critical bug"
   git push origin main v0.2.1
   ```

## Tag Naming Convention

### ✅ Correct Tag Names
- `v0.2.0` - Standard release
- `v1.0.0-rc1` - Release candidate
- `v1.0.0-beta.1` - Beta release
- `v1.0.0-alpha.1` - Alpha release

### ❌ Incorrect Tag Names
- `0.2.0` - Missing 'v' prefix
- `release-0.2.0` - Wrong prefix
- `version-0.2.0` - Wrong prefix

## Checking Current Version

### From Git
```bash
# Latest tag
git describe --tags --abbrev=0

# Current version (with commit info)
git describe --long --tags

# All tags
git tag -l
```

### From AUR Package
```bash
# Check version that would be built
cd /path/to/PKGBUILD
makepkg --printsrcinfo | grep pkgver
```

## Updating AUR Package

When you create a new release tag:

1. **AUR users with `-git` packages get updates automatically**
   - They run `yay -Syu` or `paru -Syu`
   - The `pkgver()` function pulls latest commits
   - No manual update needed for `-git` packages

2. **For stable AUR package** (if you create one later):
   - Update `pkgver` in PKGBUILD
   - Update `sha256sums` if needed
   - Update `.SRCINFO`: `makepkg --printsrcinfo > .SRCINFO`
   - Commit to AUR repository

## Version Bump Checklist

Before creating a new version tag:

- [ ] All tests pass (`make test` if available)
- [ ] Documentation updated (README, docs/)
- [ ] CHANGELOG updated (if you maintain one)
- [ ] Version number updated in Makefile
- [ ] All commits pushed to main
- [ ] Branch is clean (no uncommitted changes)

After creating tag:

- [ ] Tag pushed to GitHub
- [ ] GitHub Release created (optional)
- [ ] Announcement made (if major release)

## Automation (Future)

Consider adding a release script:

```bash
#!/bin/bash
# scripts/release.sh
VERSION=$1
if [ -z "$VERSION" ]; then
    echo "Usage: ./scripts/release.sh 0.3.0"
    exit 1
fi

echo "Creating release v$VERSION..."
sed -i "s/VERSION := .*/VERSION := $VERSION/" Makefile
git add Makefile
git commit -m "chore: bump version to v$VERSION"
git tag -a "v$VERSION" -m "Release $VERSION"
git push origin main "v$VERSION"
echo "✓ Release v$VERSION created!"
```

## Troubleshooting

### Problem: AUR package shows wrong version

**Solution:**
```bash
# Delete local tag and recreate
git tag -d v0.2.0
git tag -a v0.2.0 -m "Release 0.2.0"
git push origin :refs/tags/v0.2.0  # Delete remote
git push origin v0.2.0             # Push new tag
```

### Problem: Need to update a tag's message

**Solution:**
```bash
# Update tag annotation
git tag -a -f v0.2.0 -m "New message"
git push origin v0.2.0 --force
```

### Problem: Forgot to push tag

**Solution:**
```bash
# Push specific tag
git push origin v0.2.0

# Or push all tags
git push --tags
```

## Best Practices

1. **Always use annotated tags** (`-a` flag)
   - Contains tagger info and message
   - Better for releases

2. **Write meaningful tag messages**
   - Include major changes
   - Reference issues if applicable

3. **Don't delete or force-update tags** unless absolutely necessary
   - Users may have already pulled them
   - Can break AUR installations

4. **Follow semantic versioning strictly**
   - Makes version numbers predictable
   - Users understand impact of updates

5. **Tag from main branch only**
   - Ensures stable, reviewed code
   - Avoid tagging feature branches

## Version History

- `v0.2.0` - 2024-01-24 - Shadertoy support, crash prevention, fade transitions
- `v0.1.0` - Initial release with basic wallpaper support

## Resources

- [Semantic Versioning](https://semver.org/)
- [Git Tagging](https://git-scm.com/book/en/v2/Git-Basics-Tagging)
- [AUR Package Guidelines](https://wiki.archlinux.org/title/AUR_submission_guidelines)
- [PKGBUILD Guidelines](https://wiki.archlinux.org/title/PKGBUILD)