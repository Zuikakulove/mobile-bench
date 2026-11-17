package com.mobilebench.app

import android.os.Bundle
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.mobilebench.app.ui.BenchmarkScreen
import com.mobilebench.app.ui.MobileBenchTheme

class MainActivity : ComponentActivity() {
    private val viewModel: BenchmarkViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MobileBenchTheme {
                BenchmarkScreen(viewModel)
            }
        }
    }

    // 計測中にアプリが前面を離れたら、計測を中止して無効にする (docs/PHASE1_PLAN.md P3.10、J6)。
    // 画面の回転などの構成変更でも onStop は呼ばれるので、isChangingConfigurations で除く
    override fun onStop() {
        super.onStop()
        if (!isChangingConfigurations) viewModel.onLeftForeground()
    }

    // 画面に指が触れた時点を記録する (docs/PHASE1_PLAN.md P3.11)。記録するだけで、操作は妨げない
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (ev.actionMasked == MotionEvent.ACTION_DOWN) viewModel.onTouchDown()
        return super.dispatchTouchEvent(ev)
    }
}
