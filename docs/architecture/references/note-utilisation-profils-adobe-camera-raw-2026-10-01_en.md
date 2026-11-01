# License Note — Adobe Camera Raw DCP/LCP Profiles

## Prudent Conclusion

Research has found **no primary Adobe license explicitly authorizing IPR to redistribute on GitHub** the `.dcp` and `.lcp` files installed in `C:\ProgramData\Adobe\CameraRaw`. Do not commit, include in an installer, or provide a "fork" of this folder without written permission from Adobe or a license accompanying the specific files.

This note is a technical analysis of public sources, not legal advice. Commercial or public distribution must be validated by legal counsel and, ideally, by Adobe.

## What Adobe Sources Authorize or Establish

- Adobe documents manual installation of DCP in `C:\ProgramData\Adobe\CameraRaw\CameraProfiles` so that they are used by Camera Raw. This establishes usage and local copying within the Adobe ecosystem, not redistribution rights by another application. [Adobe Instructions for DCP Profiles](https://helpx.adobe.com/camera-raw/desktop/edit-and-enhance-images/tone-and-color/adjust-color-rendering-camera-camera.html)
- The DNG specification patent license covers implementations that read and write conforming DNG files. It does not grant a license on creative tables or Adobe profile files distributed with Camera Raw. [DNG Specification and Adobe License](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html)
- Adobe publishes a separate bundling license for its Adobe RGB ICC profile, with precise redistribution conditions. It does not target DCP or LCP, but confirms that redistribution rights for an Adobe profile must not be presumed from the mere fact that it is locally installed. [Adobe RGB ICC Distribution License](https://www.adobe.com/support/downloads/iccprofiles/icc_eula_win_dist.html)

## Applied IPR Policy

1. IPR reads Adobe profiles directly from their installed location: no copying, modification, or distribution by IPR.
2. `profiles/dcp/` and `profiles/lcp/` are ignored by Git. They are reserved for profiles whose provenance and usage rights have been verified by the user or project.
3. The repository only versions `profiles/index.json`, `.gitkeep`, possible hashes, and synthetic test profiles.
4. To publish a profile, preserve its license, author, origin, and proof of redistribution rights in a separate manifest.

Profiles created by IPR from its own measurements, or obtained under a compatible redistribution license, can be managed separately after verifying this license.
