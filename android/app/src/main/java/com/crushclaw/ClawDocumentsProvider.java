package com.crushclaw;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import androidx.annotation.Nullable;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Claw DocumentsProvider — exposes the agent's internal data directory
 * (.crush-claw/) to the system Files app sidebar.
 */
public class ClawDocumentsProvider extends DocumentsProvider {
    private static final String ROOT_ID = "claw_root";

    /* The real data directory is a hidden path (.crush-claw).  Map it
       to a non-hidden name so the native Files app doesn't suppress its
       contents. */
    private static final String HIDDEN_SEG = ".crush-claw";
    private static final String EXPOSED_SEG = "crush-claw_data";

    private static final String[] DEFAULT_ROOT_PROJECTION = new String[]{
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_MIME_TYPES,
            DocumentsContract.Root.COLUMN_FLAGS,
            DocumentsContract.Root.COLUMN_ICON,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_SUMMARY,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_AVAILABLE_BYTES,
    };

    private static final String[] DEFAULT_DOCUMENT_PROJECTION = new String[]{
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_FLAGS,
            DocumentsContract.Document.COLUMN_SIZE,
    };

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor queryRoots(String[] projection) throws FileNotFoundException {
        File clawPath = getClawRootPath();

        final MatrixCursor result = new MatrixCursor(resolveRootProjection(projection));
        if (!clawPath.exists()) {
            return result;
        }

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(DocumentsContract.Root.COLUMN_ROOT_ID, ROOT_ID);
        row.add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, getDocIdForFile(clawPath));
        row.add(DocumentsContract.Root.COLUMN_SUMMARY, "Crush Claw agent data");
        row.add(DocumentsContract.Root.COLUMN_FLAGS,
                DocumentsContract.Root.FLAG_SUPPORTS_CREATE |
                DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD |
                DocumentsContract.Root.FLAG_LOCAL_ONLY);
        row.add(DocumentsContract.Root.COLUMN_TITLE, "Crush Claw");
        row.add(DocumentsContract.Root.COLUMN_MIME_TYPES, "*/*");
        row.add(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES, clawPath.getFreeSpace());
        row.add(DocumentsContract.Root.COLUMN_ICON, R.mipmap.ic_launcher);

        return result;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(resolveDocumentProjection(projection));
        includeFile(result, documentId, null);
        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(resolveDocumentProjection(projection));

        File parent;
        try {
            parent = getFileForDocId(parentDocumentId);
        } catch (FileNotFoundException e) {
            return result;
        }

        File[] files = parent.listFiles();
        if (files != null) {
            for (File file : files) {
                try {
                    includeFile(result, null, file);
                } catch (Exception e) {
                    continue;
                }
            }
        }
        return result;
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, @Nullable CancellationSignal signal)
            throws FileNotFoundException {
        final File file = getFileForDocId(documentId);
        final int accessMode = ParcelFileDescriptor.parseMode(mode);
        return ParcelFileDescriptor.open(file, accessMode);
    }

    @Override
    public String createDocument(String parentDocumentId, String mimeType, String displayName)
            throws FileNotFoundException {
        File parent = getFileForDocId(parentDocumentId);
        File file = new File(parent, displayName);

        try {
            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
                if (!file.mkdir()) {
                    throw new FileNotFoundException("Failed to create directory");
                }
            } else {
                if (!file.createNewFile()) {
                    throw new FileNotFoundException("Failed to create file");
                }
            }
        } catch (Exception e) {
            throw new FileNotFoundException("Failed to create document: " + e.getMessage());
        }

        return getDocIdForFile(file);
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        File file = getFileForDocId(documentId);
        if (!file.delete()) {
            throw new FileNotFoundException("Failed to delete document");
        }
    }

    @Override
    public String renameDocument(String documentId, String displayName) throws FileNotFoundException {
        File file = getFileForDocId(documentId);
        File target = new File(file.getParentFile(), displayName);

        if (!file.renameTo(target)) {
            throw new FileNotFoundException("Failed to rename document");
        }

        return getDocIdForFile(target);
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        try {
            File parent = getFileForDocId(parentDocumentId);
            File child = getFileForDocId(documentId);

            String parentPath = parent.getAbsolutePath();
            String childPath = child.getAbsolutePath();

            if (!parentPath.endsWith("/")) {
                parentPath += "/";
            }

            return childPath.startsWith(parentPath);
        } catch (FileNotFoundException e) {
            return false;
        }
    }

    private File getClawRootPath() {
        File filesDir = getContext().getFilesDir();
        return new File(filesDir, HIDDEN_SEG);
    }

    /* Map the real hidden path → exposed non-hidden document ID */
    private String getDocIdForFile(File file) {
        return file.getAbsolutePath().replace("/" + HIDDEN_SEG, "/" + EXPOSED_SEG);
    }

    /* Map exposed document ID → real hidden path */
    private File getFileForDocId(String docId) throws FileNotFoundException {
        /* Native Files app sometimes passes ROOT_ID ("claw_root") as the
           parentDocumentId instead of the file-path document ID.  Map it
           back to the real root directory. */
        if (ROOT_ID.equals(docId)) {
            File root = getClawRootPath();
            if (!root.exists()) {
                throw new FileNotFoundException("Root not found: " + root.getAbsolutePath());
            }
            return root;
        }
        String realPath = docId.replace("/" + EXPOSED_SEG, "/" + HIDDEN_SEG);
        File target = new File(realPath);
        if (!target.exists()) {
            throw new FileNotFoundException("File not found: " + realPath);
        }
        return target;
    }

    private void includeFile(MatrixCursor result, String docId, File file)
            throws FileNotFoundException {
        if (docId == null) {
            docId = getDocIdForFile(file);
        } else {
            file = getFileForDocId(docId);
        }

        int flags = 0;

        if (file.isDirectory()) {
            if (file.canWrite()) {
                flags |= DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE;
            }
        } else if (file.canWrite()) {
            flags |= DocumentsContract.Document.FLAG_SUPPORTS_WRITE;
            flags |= DocumentsContract.Document.FLAG_SUPPORTS_DELETE;
        }

        if (file.canWrite()) {
            flags |= DocumentsContract.Document.FLAG_SUPPORTS_DELETE;
            flags |= DocumentsContract.Document.FLAG_SUPPORTS_RENAME;
        }

        String displayName = file.getName();
        /* Strip leading dot so the root directory appears as "crush-claw"
           instead of ".crush-claw" in the system file manager. */
        if (displayName.startsWith(".")) displayName = displayName.substring(1);
        final String mimeType = getTypeForFile(file);

        if (mimeType.startsWith("image/")) {
            flags |= DocumentsContract.Document.FLAG_SUPPORTS_THUMBNAIL;
        }

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, docId);
        row.add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, displayName);
        row.add(DocumentsContract.Document.COLUMN_SIZE, file.length());
        row.add(DocumentsContract.Document.COLUMN_MIME_TYPE, mimeType);
        row.add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, file.lastModified());
        row.add(DocumentsContract.Document.COLUMN_FLAGS, flags);
    }

    private static String getTypeForFile(File file) {
        if (file.isDirectory()) {
            return DocumentsContract.Document.MIME_TYPE_DIR;
        } else {
            final String name = file.getName();
            final int lastDot = name.lastIndexOf('.');
            if (lastDot >= 0) {
                final String extension = name.substring(lastDot + 1).toLowerCase();
                final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
                if (mime != null) {
                    return mime;
                }
            }
            return "application/octet-stream";
        }
    }

    private static String[] resolveRootProjection(String[] projection) {
        return projection != null ? projection : DEFAULT_ROOT_PROJECTION;
    }

    private static String[] resolveDocumentProjection(String[] projection) {
        return projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION;
    }
}
