# Third-party licences and IPR licence options

This directory contains licence texts and a working inventory of the third-party
components currently used or considered by IPR. It is not a substitute for the
licence notices supplied with the exact binaries distributed in a release.

## Component inventory

| Component | IPR use | Licence / distribution notes |
|---|---|---|
| Qt 6 (`Core`, `Gui`, `Quick`, `Concurrent`, QML modules) | Application framework | Qt Community Edition components are offered under LGPLv3, GPLv3, or a commercial Qt licence, subject to the module-specific terms. IPR uses Qt under the applicable LGPL option. A distributor must provide required notices and source for LGPL-covered Qt modifications, permit replacement/relinking of the LGPL library, and meet the other LGPL conditions. Verify each module and the exact Qt build before release. |
| OpenCV | Image-processing library | Apache License 2.0. Preserve copyright, licence and NOTICE notices, and mark modified files as required. Apache-2.0 includes an express patent grant and patent-termination clause. |
| LibRaw | Optional CR2/CR3 and other RAW decoding | Upstream offers the library under LGPL-2.1-or-later or CDDL-1.0. The exact selected licence and any separately licensed files depend on the source/package used to build and distribute IPR. Record that choice and retain its notices. |
| IPR camera-profile engine | DCP camera rendering and limited LCP radial-distortion processing | Implemented in IPR source without copying or linking a third-party DCP/LCP engine. Adobe profile files remain separate user-supplied data; check the profile vendor's terms before redistributing them. |
| Adobe DNG SDK | Considered for DCP camera-profile support | Adobe DNG SDK License Agreement (included here) is a custom, non-OSI-approved licence, not an open-source licence. It grants broad use and redistribution rights but includes restrictions/notices, an “AS IS” disclaimer, and an indemnification obligation for distribution in a commercial product. Do not describe a build that includes this SDK as consisting exclusively of open-source components. |

## Current project licensing status

No top-level `LICENSE` has been selected or added by this inventory. In
particular, adding the LGPL text here does **not** place IPR itself under the
LGPL. A project-level licence should be selected by the copyright holders and
added explicitly before publishing source or binaries.

The DNG SDK is not currently integrated into the production build. A future
integration must remain an explicit, optional dependency and preserve Adobe's
licence notice. The SDK's custom terms require a separate compatibility review
before combining it with an IPR release under LGPL, GPL, or another OSI licence.

## Choosing a licence for IPR

| Licence | Main advantage | Main trade-off | Fit for IPR |
|---|---|---|---|
| LGPL-3.0-or-later | Keeps modifications to IPR available under copyleft while permitting proprietary applications to use the library under the LGPL conditions. | Distribution of a combined application still requires careful compliance: notices, relinking/replacement rights, and disclosure of modifications to LGPL-covered components. It does not itself make a custom-licensed dependency open source. | A reasonable choice if the intent is to encourage a shared editor while allowing commercial use, after checking Qt and optional SDK compatibility. |
| LGPL-2.1-or-later | Similar library-level copyleft with a version choice that can ease use with older ecosystems. | Older terms have less explicit installation/relinking language than LGPLv3; Apache-2.0 combinations need a version-specific compatibility review. | Possible, but LGPL-3.0-or-later is generally clearer for a new Qt 6 project. |
| GPL-3.0-or-later | Strong copyleft: distributed derivatives of IPR generally need to remain under GPL-compatible terms. | Less suitable if proprietary extensions or closed integrations are intended. Additional proprietary SDK terms can create a serious compatibility question. | Suitable only if the goal is to require downstream distributed editor derivatives to remain free software. |
| MIT | Very simple permissive terms; broad reuse with minimal conditions. | Does not require downstream modifications to stay open, and has no express patent grant comparable to Apache-2.0. | Good when maximum reuse is more important than copyleft. |
| Apache-2.0 | Permissive, with explicit patent grants and patent-litigation termination terms. | More notice and patent language than MIT; does not require downstream source publication. | A strong permissive alternative for an application/library where patent clarity is desired. |

### Practical recommendation

For the stated goal—an open-source Qt photo editor whose improvements remain
available while allowing commercial use—**LGPL-3.0-or-later** is a reasonable
starting point for IPR's own code. Keep the DNG SDK optional and separately
licensed; do not bundle it in an LGPL-only distribution until a qualified
licensing review confirms the intended combination and release obligations.
If the DNG SDK is mandatory in every build, resolve that licensing issue before
declaring an OSI-approved project licence.

These notes are technical project guidance, not legal advice. The copyright
holders should review the exact Qt modules, LibRaw package, build/link mode,
redistribution method, and Adobe SDK terms with counsel before a public or
commercial release.

## Primary sources

- Qt licensing: <https://www.qt.io/licensing/open-source-lgpl-obligations>
- Qt module licences: <https://doc.qt.io/qt-6/licensing.html>
- OpenCV licence: <https://github.com/opencv/opencv/blob/4.x/LICENSE>
- LibRaw licence choices: <https://github.com/LibRaw/LibRaw/blob/master/COPYRIGHT>
- LibRaw LGPL text: <https://github.com/LibRaw/LibRaw/blob/master/LICENSE.LGPL>
- LibRaw CDDL text: <https://github.com/LibRaw/LibRaw/blob/master/LICENSE.CDDL>
- Adobe DNG SDK: <https://www.adobe.com/go/dng_sdk>
- GNU LGPL 2.1: <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
- GNU LGPL 3: <https://www.gnu.org/licenses/lgpl-3.0.html>
- GNU GPL 3: <https://www.gnu.org/licenses/gpl-3.0.html>
- MIT licence: <https://opensource.org/license/mit>
- Apache 2.0 licence: <https://www.apache.org/licenses/LICENSE-2.0>
