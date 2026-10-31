package net.ryuya_dev.tailadb

import android.os.Bundle
import android.view.View
import android.view.View.OnClickListener
import android.widget.LinearLayout
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.OnApplyWindowInsetsListener
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsCompat.Type
import androidx.core.widget.NestedScrollView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.color.DynamicColors
import com.google.android.material.textview.MaterialTextView
import java.nio.charset.StandardCharsets
import kotlin.math.round

class LicensesActivity : AppCompatActivity() {
    override fun onCreate(saved: Bundle?) {
        DynamicColors.applyToActivityIfAvailable(this)
        super.onCreate(saved)
        val root = LinearLayout(this)
        root.setOrientation(LinearLayout.VERTICAL)
        setContentView(root)
        ViewCompat.setOnApplyWindowInsetsListener(
            root,
            OnApplyWindowInsetsListener { v: View?, insets: WindowInsetsCompat? ->
                val p = insets!!.getInsets(Type.systemBars() or Type.displayCutout())
                v!!.setPadding(p.left, p.top, p.right, p.bottom)
                insets!!
            },
        )
        val toolbar = MaterialToolbar(this)
        toolbar.setTitle(R.string.licenses)
        toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material)
        toolbar.setNavigationOnClickListener(OnClickListener { v: View? -> finish() })
        root.addView(toolbar)
        val scroll = NestedScrollView(this)
        root.addView(scroll, android.widget.LinearLayout.LayoutParams(-1, 0, 1f))
        val text = MaterialTextView(this)
        text.setTextIsSelectable(true)
        val padding = Math.round(20 * getResources().getDisplayMetrics().density)
        text.setPadding(padding, padding, padding, padding)
        scroll.addView(text, android.view.ViewGroup.LayoutParams(-1, -2))
        try {
            text.setText(
                String(getAssets().open("LICENSES.txt").readAllBytes(), StandardCharsets.UTF_8)
            )
        } catch (e: Exception) {
            text.setText(e.javaClass.getSimpleName())
        }
    }
}
