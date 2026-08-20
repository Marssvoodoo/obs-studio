# OBS 32.2.2 Integration Design

## Approach

Merge the official 32.2 release branch with a merge commit. This preserves
upstream ancestry and imports the 32.2.2 version metadata. The only Windows
runtime hotfix is already present locally as patch-equivalent commit
`df0d3b4adb5357e2d49a3661f8f62f7eb3cf0748`, so no duplicate manual patch is
needed.

Update the two Windows build entry points, release documentation, and public
README to the 32.2.2 baseline. Keep the custom version suffix so third-party
plug-ins see the canonical major version while logs remain distinguishable.

## Risk controls

- Preserve the pre-merge commit with a dated backup branch.
- Inspect the complete merge delta and fork-specific file overlap.
- Verify the retained DLL directory policy and absence of
  `PreferSystem32Images` in executable source.
- Build from the existing configured Release tree and run CTest.
- Back up the installed tree before copying the verified build.
- Compare build and installed executable hashes before runtime acceptance.

## Rollback

Restore the timestamped Program Files backup for deployment rollback. The
dated backup Git branch retains the exact pre-merge source state.
