# Publishing / Release process

This document captures how `tokenizers` is built, tagged, and published. The
GitHub side (repo, CI, docs site, GitHub release, source tarball) is fully
automated. The two third-party **registries** (Packagist, pecl.php.net) require
the maintainer's own account credentials and cannot be automated by a tool —
the turnkey commands are below.

## Status of v0.1.0

| Step | State |
|---|---|
| Public repo `github.com/webrek/tokenizers` | ✅ done |
| CI green (PHP 8.3/8.4 × NTS/ZTS × macOS+Ubuntu) | ✅ done |
| Docs site `https://webrek.github.io/tokenizers/` | ✅ done |
| Git tag `v0.1.0` + GitHub Release | ✅ done |
| PECL source tarball `tokenizers-0.1.0.tgz` (release asset, verified `pecl install`) | ✅ done |
| `composer.json` valid (`type: php-ext`) | ✅ done |
| **Packagist** listing (`composer require` / `pie install`) | ⬜ needs maintainer login |
| **pecl.php.net** channel (`pecl install tokenizers`) | ⬜ needs PECL account + community review |

## Cut a release (maintainer)

```bash
# 1. Bump <version> in package.xml and the version badges/strings, commit.
# 2. Rebuild and validate the PECL source package:
pecl package package.xml
pear package-validate package.xml          # legacy parser flags PHP 8 syntax as
                                           # "invalid PHP" — those are false positives
# 3. Verify it actually installs from the tarball:
pecl install --force tokenizers-<version>.tgz
php -m | grep tokenizers
# 4. Tag and publish the GitHub release with the tarball attached:
git tag -a v<version> -m "tokenizers <version>"
git push origin v<version>
gh release create v<version> tokenizers-<version>.tgz --title "v<version>" --notes-file NOTES.md
```

The `docs` GitHub Actions workflow redeploys the site on every push to `main`
that touches `docs/**` or `mkdocs.yml`.

## Publish to Packagist (maintainer credentials required)

**Web (simplest):** sign in at <https://packagist.org>, open
<https://packagist.org/packages/submit>, paste
`https://github.com/webrek/tokenizers`, and Submit. Then enable the
GitHub → Packagist auto-update integration (GitHub app or webhook) so pushes and
new tags update the listing automatically.

**API (one command):** get the token from <https://packagist.org/profile/> →
"Show API Token", then:

```bash
curl -X POST \
  "https://packagist.org/api/create-package?username=<your-packagist-user>&apiToken=<token>" \
  -H "Content-Type: application/json" \
  -d '{"repository":{"url":"https://github.com/webrek/tokenizers"}}'
```

After submission, `composer require webrek/tokenizers` and
`pie install webrek/tokenizers` resolve from the registry.

## Publish to pecl.php.net (maintainer account + review)

Unlike the GitHub release, the PECL **channel** entry goes through a human
process and cannot be completed in one step:

1. Request an account at <https://pecl.php.net/account-request.php>.
2. Propose the package: <https://pecl.php.net/package-new.php> (fill in
   category, summary, etc.). This enters a proposal/voting period by the PECL
   developers — typically several days.
3. Once approved, upload the release tarball (the same
   `tokenizers-<version>.tgz`) via the maintainer dashboard.

Only after that does `pecl install tokenizers` (the short form) work. Until
then, install from the release tarball:

```bash
pecl install https://github.com/webrek/tokenizers/releases/download/v0.1.0/tokenizers-0.1.0.tgz
```
