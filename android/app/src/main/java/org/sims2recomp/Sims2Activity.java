package org.sims2recomp;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.util.concurrent.CountDownLatch;

public final class Sims2Activity extends SDLActivity {
    private static final int REQUEST_ALL_FILES = 4102;
    private final CountDownLatch storagePermissionResult = new CountDownLatch(1);

    @Override
    protected void main() {
        if (!hasGameFolderAccess()) {
            // SDL invokes main() on its native thread. Wait there, not on the UI
            // thread that needs to open Settings and deliver the permission result.
            runOnUiThread(this::requestGameFolderAccess);
            try {
                storagePermissionResult.await();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                runOnUiThread(this::finish);
                return;
            }
        }

        if (!hasGameFolderAccess()) {
            runOnUiThread(() -> {
                Toast.makeText(this, "Storage access is required to read /storage/emulated/0/Sims2.",
                        Toast.LENGTH_LONG).show();
                finish();
            });
            return;
        }
        super.main();
    }

    private boolean hasGameFolderAccess() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager();
    }

    private void requestGameFolderAccess() {
        Toast.makeText(this,
                "Allow file access so the game can read your legally owned files in /storage/emulated/0/Sims2.",
                Toast.LENGTH_LONG).show();
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
        intent.setData(Uri.parse("package:" + getPackageName()));
        try {
            startActivityForResult(intent, REQUEST_ALL_FILES);
        } catch (android.content.ActivityNotFoundException e) {
            Intent fallback = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            try {
                startActivityForResult(fallback, REQUEST_ALL_FILES);
            } catch (android.content.ActivityNotFoundException ignored) {
                // Do not leave SDL's native startup thread waiting forever when
                // this Android build exposes neither all-files settings screen.
                storagePermissionResult.countDown();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_ALL_FILES) {
            storagePermissionResult.countDown();
        }
    }
}
