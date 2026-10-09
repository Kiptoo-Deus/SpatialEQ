# Publishing SpatialEQ on Google Play

Everything in this folder is ready to paste or upload. The steps that need your Google accounts are marked **(you)**.

## 1. Accounts (you)

1. **Google Play Console**: https://play.google.com/console. Create a developer account for **Savannah DSP**: a one-time US$25 fee plus identity verification.
   - An **organization** account needs a D-U-N-S number.
   - A **personal** account must run a closed test with at least 12 testers for 14 days before it can publish to production.
2. **AdMob**: https://admob.google.com. Sign in with the same Google account and add a payment profile, which is how ad money is paid out.
   1. *Apps → Add app → Android → "No, it's not listed yet"*, name it **SpatialEQ**.
   2. Copy the **App ID** (`ca-app-pub-…~…`).
   3. *Ad units → Add ad unit → Banner*, name it `home_banner`, then copy its **Ad unit ID** (`ca-app-pub-…/…`).
   4. *Privacy & messaging → GDPR → Create message*: publish the European consent message. The app already shows it through Google's UMP SDK.

## 2. Put the real AdMob IDs in the app

In `android/gradle.properties`, replace the two **test** IDs:

```properties
ADMOB_APP_ID=ca-app-pub-XXXXXXXXXXXXXXXX~XXXXXXXXXX
ADMOB_BANNER_ID=ca-app-pub-XXXXXXXXXXXXXXXX/XXXXXXXXXX
```

Then rebuild the signed bundle:

```bash
cd android && ./gradlew :app:bundleRelease
```

The output is `android/app/build/outputs/bundle/release/app-release.aab`.

Never tap your own live ads: AdMob bans accounts for invalid clicks. On your own phone, add it as a test device under *AdMob → Settings → Test devices*.

## 3. Create the app in Play Console (you)

1. *Create app*:
   - Name **SpatialEQ: EQ & 3D Sound**
   - Default language English
   - App, Free
   - Accept the declarations.
2. *Release → Testing → Internal testing → Create release*:
   - Upload `app-release.aab` and opt in to **Play App Signing**.
   - Release notes: "First release."
3. Fill in each *App content* item using section 4.
4. *Store presence → Main store listing*: paste the text from `listing.md`, then upload `icon-512.png`, `feature-graphic.png` and the phone screenshots in `screenshots/`.
5. Promote internal testing to closed testing, then production. A personal account must meet the 12-testers / 14-days rule first.

## 4. App content answers

**Privacy policy URL**: https://savannah-dsp.github.io/SpatialEQ/privacy-policy.html

**Ads**: Yes, the app contains ads.

**App access**: All functionality is available without special access. In the review notes, mention that system-wide processing needs Android's screen-capture confirmation. Optionally explain the `adb shell pm grant com.savannahdsp.spatialeq android.permission.DUMP` step.

**Content rating**: Utility / productivity questionnaire. No violence, sexual content, gambling or user-generated content. The expected rating is Everyone / PEGI 3.

**Target audience**: 13 and over. Do not select ages under 13: that would place the app under the Families policy and restrict ads.

**Data safety**: SpatialEQ itself collects nothing; audio never leaves the device. The AdMob SDK collects the following. Check it against Google's current guide: https://developers.google.com/admob/android/privacy/play-data-disclosure

| Data type | Collected | Shared | Purpose |
|---|---|---|---|
| Location → Approximate location (from IP address) | Yes | Yes | Advertising, Analytics, Fraud prevention |
| App activity → App interactions | Yes | Yes | Advertising, Analytics |
| App info and performance → Diagnostics | Yes | Yes | Analytics, Fraud prevention |
| Device or other IDs (advertising ID) | Yes | Yes | Advertising, Analytics, Fraud prevention |

Other answers:
- Data is encrypted in transit: **Yes**.
- Users can request data deletion: **No**. SpatialEQ holds no user data, and ads data is handled by Google.
- Audio, files and personal info: **not collected**. Processing happens on the device only and nothing is stored.

**Foreground service declaration** (*App content → Foreground service permissions*): select **Media projection**.
- Description: "SpatialEQ captures other apps' audio playback using Android's AudioPlaybackCapture API, applies the user's equalizer and spatial-audio settings in real time and plays the result. The user starts and stops processing in the app; a persistent notification with a Stop button is shown while it runs. No screen content is captured."
- Attach a short screen recording: tap Start, accept the prompt, play music and toggle the effect.

**Sensitive permissions**:
- `RECORD_AUDIO` is required by Android for playback capture. The app never opens the microphone.
- `DUMP` is a development permission that users can only grant themselves over ADB. It's declared for session discovery, and the app works without it.

## 5. After launch

- Watch *Android vitals* for crashes and ANRs.
- Earnings appear in AdMob. Payments start once the balance reaches the payout threshold, after tax and payment information are verified in AdMob.
- For every update, increase `versionCode` and `versionName` in `android/app/build.gradle.kts`, rebuild the bundle and upload it as a new release.

## Upload key

The release bundle is signed with the upload key in `~/Documents/SavannahDSP-keys/spatialeq-upload.jks`. Its password is in `spatialeq-upload.properties` next to it, and `android/keystore.properties` points the build at it. **Back up both files somewhere safe and private**, such as a password manager or an encrypted drive. With Play App Signing, a lost upload key can be reset through Play support, but that takes time.
