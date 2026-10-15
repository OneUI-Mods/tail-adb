package net.ryuya_dev.tailadb;

import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import androidx.core.widget.NestedScrollView;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.textview.MaterialTextView;
import java.nio.charset.StandardCharsets;

public final class LicensesActivity extends AppCompatActivity {
  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    setContentView(root);
    ViewCompat.setOnApplyWindowInsetsListener(
        root,
        (v, insets) -> {
          var p =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          v.setPadding(p.left, p.top, p.right, p.bottom);
          return insets;
        });
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.licenses);
    toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material);
    toolbar.setNavigationOnClickListener(v -> finish());
    root.addView(toolbar);
    NestedScrollView scroll = new NestedScrollView(this);
    root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    MaterialTextView text = new MaterialTextView(this);
    text.setTextIsSelectable(true);
    int padding = Math.round(20 * getResources().getDisplayMetrics().density);
    text.setPadding(padding, padding, padding, padding);
    scroll.addView(text, new ViewGroup.LayoutParams(-1, -2));
    try {
      text.setText(
          new String(getAssets().open("LICENSES.txt").readAllBytes(), StandardCharsets.UTF_8));
    } catch (Exception e) {
      text.setText(e.getClass().getSimpleName());
    }
  }
}
