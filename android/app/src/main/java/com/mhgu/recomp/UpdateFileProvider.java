package com.mhgu.recomp;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

/** Read-only, grant-only content URI used to hand a downloaded APK to Android's package installer. */
public final class UpdateFileProvider extends ContentProvider {
    private static final String APK_MIME = "application/vnd.android.package-archive";

    @Override
    public boolean onCreate() {
        return true;
    }

    private boolean isUpdateApk(Uri uri) {
        if (uri == null) return false;
        java.util.List<String> parts = uri.getPathSegments();
        return parts.size() == 2 && "apk".equals(parts.get(0)) && "app-update.apk".equals(parts.get(1));
    }

    private File updateApk() throws FileNotFoundException {
        if (getContext() == null) throw new FileNotFoundException("No app context.");
        File root = new File(getContext().getCacheDir(), "updates");
        File apk = new File(root, "app-update.apk");
        try {
            String rootPath = root.getCanonicalPath() + File.separator;
            if (!apk.getCanonicalPath().startsWith(rootPath) || !apk.isFile() || !apk.canRead())
                throw new FileNotFoundException("The update APK is not available.");
        } catch (IOException e) {
            throw new FileNotFoundException(e.getMessage());
        }
        return apk;
    }

    @Override
    public String getType(Uri uri) {
        return isUpdateApk(uri) ? APK_MIME : null;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        if (!isUpdateApk(uri)) return null;
        try {
            File apk = updateApk();
            String[] columns = projection == null
                    ? new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}
                    : projection;
            MatrixCursor cursor = new MatrixCursor(columns, 1);
            Object[] row = new Object[columns.length];
            for (int i = 0; i < columns.length; i++) {
                if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) row[i] = apk.getName();
                else if (OpenableColumns.SIZE.equals(columns[i])) row[i] = apk.length();
            }
            cursor.addRow(row);
            return cursor;
        } catch (FileNotFoundException e) {
            return null;
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!isUpdateApk(uri) || !"r".equals(mode))
            throw new FileNotFoundException("Only the verified update APK can be read.");
        return ParcelFileDescriptor.open(updateApk(), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Read only.");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read only.");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read only.");
    }
}
