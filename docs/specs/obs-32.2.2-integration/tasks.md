# OBS 32.2.2 Integration Tasks

- [x] Capture current local, remote, installed, and process state.
- [x] Create a dated source rollback branch.
- [x] Merge official `upstream/release/32.2`.
- [x] Update Windows version overrides and release documentation.
- [x] Review the merged delta, security policy, and fork feature markers.
- [x] Build Release and run registered tests.
- [x] Back up and deploy the verified Windows tree.
- [ ] Run non-streaming startup and normal-exit acceptance. Startup passed;
  normal interactive exit remains unverified because the hidden-window close
  was not accepted and the multi-window fallback was an invalid test.
- [ ] Commit the integration documentation and push the fork branch.
- [ ] Verify local/remote SHA equality and record final evidence.
