# Newspaper source and TLS provenance

The image pattern was first verified 2026-09-30 and checked again 2026-10-15.
This standalone version retrieves the New York Times print
front page from **Freedom Forum's public Today's Front Pages service**. It does
not use a screenshot, summarize stories, or manufacture a replacement edition.

## Primary provider evidence

- [Freedom Forum Today's Front Pages](https://frontpages.freedomforum.org/)
- [The New York Times front page on Freedom Forum](https://frontpages.freedomforum.org/newspapers/ny_nyt-The_New_York_Times)
- [Verified September 30, 2026 large JPEG](https://d2dr22b2lm4tvw.cloudfront.net/ny_nyt/2026-09-30/front-page-large.jpg)

The newspaper's provider page explicitly displayed **Wednesday, Sep. 30, 2026**
and included that exact JPEG in its normal `img` elements, image preload, and
`og:image` metadata. Its embedded public page data also supplied
`date: "2026-09-30"` and `id: "ny_nyt-2026-09-30"`. No credentials, private API,
access-control workaround, or guessed image URL was required to discover it.

A normal HTTPS download returned:

| Property | Observed value |
| --- | --- |
| HTTP status | 200 |
| Content type | `image/jpeg` |
| File size | 1,040,794 bytes |
| Raster size | 1542 × 2958 pixels |
| JPEG coding | Baseline, 8-bit, 3 components; not progressive |
| Last-Modified | Wed, 30 Sep 2026 10:00:19 GMT |
| SHA-256 | `4aea802d32139a821183a0217b922be5a21c0430165e4dc3d74b9811d5e3881c` |

The page also links a 500 × 959 medium image. Firmware uses the large image for
better text detail. The page's generic Open Graph dimensions said 1200 × 630;
those metadata dimensions are inaccurate for the actual newspaper JPEG and are
not used. The renderer scales the print page to the panel's full width and
crops any overflow from the bottom to keep headline type more legible.

## Daily retrieval

Firmware uses the observed date-specific resource pattern:

```
https://d2dr22b2lm4tvw.cloudfront.net/ny_nyt/YYYY-MM-DD/front-page-large.jpg
```

It requests only the selected date. There is no fallback to an older date, no
pretending an older edition is current, and no re-labeling a response from a
shared latest-image URL. A failed request leaves the existing screen intact and
the application retries later.

This is a public website asset pattern, **not a promised or versioned API**.
Publication times, CDN hostname, image coding and paths may change. The current
firmware then fails closed and needs an update; it does not scrape arbitrary
replacement sources. The date derives from the exact provider resource path;
the device does not OCR the newspaper to independently confirm its printed date.
The single observed Last-Modified time is not a guarantee of daily availability.

The direct NYT Today’s Paper/PDF host returned an HTML “Site Unavailable” response
from this execution environment. This project therefore uses the separately
verified Freedom Forum public image rather than assuming that a direct NYT JPEG
URL works.

On October 15, 2026, Freedom Forum's public New York Times page identified
Thursday, Oct. 15, 2026 as its current edition. A direct HEAD request for
`https://d2dr22b2lm4tvw.cloudfront.net/ny_nyt/2026-10-15/front-page-large.jpg`
returned HTTP 200, `image/jpeg`, and 1,068,567 bytes. This confirms the dated
asset pattern was still served that day; it does not establish that future
editions or a device-side TLS handshake will succeed.

No newspaper image is bundled in this source release. Freedom Forum and the
publisher retain their own content rights; publishing source code or original
setup artwork does not grant permission to redistribute a front-page image.

## HTTPS trust

Firmware validates the CDN hostname and its certificate chain using the five
roots Amazon Trust Services recommends for a custom trust store. It never calls
`setInsecure()`. Network time must be set before making HTTPS requests.

Root certificates are copied unmodified from the
[Amazon Trust Services certificate repository](https://www.amazontrust.com/repository/).
Their DER SHA-256 fingerprints were checked against that primary repository:

| Certificate | DER SHA-256 |
| --- | --- |
| [Amazon Root CA 1](https://www.amazontrust.com/repository/AmazonRootCA1.pem) | `8ecde6884f3d87b1125ba31ac3fcb13d7016de7f57cc904fe1cb97c6ae98196e` |
| [Amazon Root CA 2](https://www.amazontrust.com/repository/AmazonRootCA2.pem) | `1ba5b2aa8c65401a82960118f80bec4f62304d83cec4713a19c39c011ea46db4` |
| [Amazon Root CA 3](https://www.amazontrust.com/repository/AmazonRootCA3.pem) | `18ce6cfe7bf14e60b2e347b8dfe868cb31d02ebb3ada271569f50343b46db3a4` |
| [Amazon Root CA 4](https://www.amazontrust.com/repository/AmazonRootCA4.pem) | `e35d28419ed02025cfa69038cd623962458da5c695fbdea3c22b0bfb25897092` |
| [Starfield Services Root CA G2](https://www.amazontrust.com/repository/SFSRootCAG2.pem) | `568d6905a2c88708a4b3025190edcfedb1974a606a13c6e5290fcb2ae63edab5` |

The certificate files are provided by Amazon Trust Services under
[CC BY-ND 4.0](https://creativecommons.org/licenses/by-nd/4.0/). They are embedded
in `firmware/src/source.cpp` without modification.

Certificate downloads and fingerprint verification succeeded on the host,
but a device-to-CDN TLS handshake has not yet been physically tested for this
edition. Do not solve a TLS failure by disabling verification; update the
trust store if the provider changes its certificate chain.

## Transfer and decoder limits

- One dated public request per attempt; HTTP redirects are disabled.
- HTTP 200, `image/jpeg`, uncompressed HTTP body, and a positive Content-Length
  no greater than 2 MiB are required.
- Download buffer is allocated in PSRAM; it is returned to the caller only when
  the full byte count has arrived.
- Network work is bounded by connection, handshake, idle and overall timeouts.
- Before decoding: JPEG start/end markers, baseline 8-bit encoding, component
  count, useful minimum resolution, and maximum dimensions/pixel count are
  checked. JPEGDEC must also successfully decode the complete stream.
- Rejected downloads do not replace the display or advance its stored edition.
