package com.savannahdsp.spatialeq.ui

import android.app.Activity
import android.content.Context
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.viewinterop.AndroidView
import com.google.android.gms.ads.AdRequest
import com.google.android.gms.ads.AdSize
import com.google.android.gms.ads.AdView
import com.google.android.gms.ads.MobileAds
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform
import com.savannahdsp.spatialeq.BuildConfig
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicBoolean

/**
 * AdMob with Google's consent flow (UMP): users in regions that require consent (EEA, UK,
 * Switzerland) see Google's consent form first; ads are only requested once allowed.
 */
object Ads {
    val ready = MutableStateFlow(false)
    val privacyOptionsRequired = MutableStateFlow(false)
    private val started = AtomicBoolean(false)
    private lateinit var consent: ConsentInformation

    fun init(activity: Activity) {
        consent = UserMessagingPlatform.getConsentInformation(activity)
        consent.requestConsentInfoUpdate(activity, ConsentRequestParameters.Builder().build(), {
            UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) {
                updatePrivacyOptions()
                if (consent.canRequestAds()) start(activity)
            }
        }, {
            // Consent info unavailable (e.g. offline): fall back to a previous decision if any.
            if (consent.canRequestAds()) start(activity)
        })
        if (consent.canRequestAds()) start(activity)
    }

    /** "Ad privacy choices": lets users change their consent later, as Google requires. */
    fun showPrivacyOptions(activity: Activity) {
        UserMessagingPlatform.showPrivacyOptionsForm(activity) { updatePrivacyOptions() }
    }

    private fun updatePrivacyOptions() {
        privacyOptionsRequired.value =
            consent.privacyOptionsRequirementStatus == ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED
    }

    private fun start(context: Context) {
        if (!started.compareAndSet(false, true)) return
        CoroutineScope(Dispatchers.IO).launch {
            MobileAds.initialize(context.applicationContext) { ready.value = true }
        }
    }
}

/** Anchored adaptive banner; renders nothing until ads are allowed and initialised. */
@Composable
fun BannerAd(modifier: Modifier = Modifier) {
    val ready by Ads.ready.collectAsState()
    if (!ready) return
    val widthDp = LocalConfiguration.current.screenWidthDp
    AndroidView(
        modifier = modifier.fillMaxWidth(),
        factory = { ctx ->
            AdView(ctx).apply {
                adUnitId = BuildConfig.ADMOB_BANNER_ID
                setAdSize(AdSize.getCurrentOrientationAnchoredAdaptiveBannerAdSize(ctx, widthDp))
                loadAd(AdRequest.Builder().build())
            }
        },
        onRelease = { it.destroy() },
    )
}
