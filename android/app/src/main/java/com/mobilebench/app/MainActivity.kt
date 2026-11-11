package com.mobilebench.app

import android.os.Bundle
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
}
