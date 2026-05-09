package com.crushclaw;

import android.util.Log;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * Android HTTP transport — replaces libcurl for LLM API calls.
 * Called from C code via JNI on native (pthread) threads.
 *
 * HttpURLConnection (and its OkHttp internals) crash on ART 14+
 * when called from a native-attached thread.  We serialize all
 * requests through a single background worker thread that has
 * proper Java thread semantics (context classloader, daemon).
 */
public class HttpTransport {

    private static final String TAG = "HttpTransport";

    private static final ExecutorService sExecutor =
            Executors.newSingleThreadExecutor(r -> {
                Thread t = new Thread(r, "http-transport");
                t.setDaemon(true);
                t.setContextClassLoader(HttpTransport.class.getClassLoader());
                return t;
            });

    public static String[] httpPostJson(
            String url, String method,
            String body,
            String apiKey, String authType,
            String[] headers,
            int timeoutMs) {

        Future<String[]> future = sExecutor.submit(() ->
                httpPostJsonImpl(url, method, body, apiKey, authType, headers, timeoutMs));

        try {
            String[] result = future.get();
            if (result != null && result[1] != null && !result[1].equals("0")) {
                Log.i(TAG, "HTTP " + result[1] + " " + url.substring(0, Math.min(60, url.length())) +
                        (result[2].isEmpty() ? "" : " err=" + result[2].substring(0, Math.min(80, result[2].length()))));
            } else if (result != null && !result[2].isEmpty()) {
                Log.w(TAG, "HTTP FAIL " + url.substring(0, Math.min(60, url.length())) +
                        " err=" + result[2].substring(0, Math.min(80, result[2].length())));
            }
            return result;
        } catch (Exception e) {
            Log.e(TAG, "HTTP crash: " + e.getClass().getSimpleName() + " " + e.getMessage());
            return new String[]{"", "0",
                    e.getClass().getSimpleName() + ": " +
                    (e.getCause() != null ? e.getCause().getMessage() : e.getMessage())};
        }
    }

    private static String[] httpPostJsonImpl(
            String url, String method,
            String body,
            String apiKey, String authType,
            String[] headers,
            int timeoutMs) {

        String[] result = new String[]{"", "0", ""};
        java.net.HttpURLConnection conn = null;

        try {
            java.net.URL targetUrl = new java.net.URL(url);
            conn = (java.net.HttpURLConnection) targetUrl.openConnection();
            conn.setRequestMethod(method != null && !method.isEmpty() ? method : "GET");
            Log.i(TAG, "REQUEST " + conn.getRequestMethod() + " " + url.substring(0, Math.min(60, url.length())));

            boolean isGetHead = "GET".equalsIgnoreCase(method) || "HEAD".equalsIgnoreCase(method);

            if (!isGetHead) {
                conn.setRequestProperty("Content-Type", "application/json");
            }
            conn.setConnectTimeout(timeoutMs > 0 ? timeoutMs : 30000);
            conn.setReadTimeout(timeoutMs > 0 ? timeoutMs : 30000);
            conn.setDoOutput(!isGetHead);
            conn.setDoInput(true);
            conn.setInstanceFollowRedirects(true);

            if (apiKey != null && !apiKey.isEmpty()) {
                String authTypeLower = (authType != null) ? authType.toLowerCase() : "bearer";
                if ("api-key".equals(authTypeLower) || "api_key".equals(authTypeLower)) {
                    conn.setRequestProperty("X-API-Key", apiKey);
                } else if (!"none".equals(authTypeLower)) {
                    conn.setRequestProperty("Authorization", "Bearer " + apiKey);
                }
            }

            if (headers != null) {
                for (String h : headers) {
                    if (h != null && !h.isEmpty()) {
                        int colon = h.indexOf(':');
                        if (colon > 0) {
                            String name = h.substring(0, colon).trim();
                            String value = h.substring(colon + 1).trim();
                            if (!name.isEmpty()) {
                                conn.setRequestProperty(name, value);
                            }
                        }
                    }
                }
            }

            if (body != null && !body.isEmpty()) {
                byte[] postData = body.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                conn.setRequestProperty("Content-Length", String.valueOf(postData.length));
                try (java.io.DataOutputStream dos = new java.io.DataOutputStream(conn.getOutputStream())) {
                    dos.write(postData);
                }
            }

            int statusCode = conn.getResponseCode();
            result[1] = String.valueOf(statusCode);

            java.io.InputStream inputStream;
            boolean isError = (statusCode >= 400);
            try {
                inputStream = isError ? conn.getErrorStream() : conn.getInputStream();
            } catch (Exception e) {
                inputStream = isError ? conn.getErrorStream() : null;
            }

            if (inputStream != null) {
                try (java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream()) {
                    byte[] buf = new byte[4096];
                    int len;
                    while ((len = inputStream.read(buf)) != -1) {
                        baos.write(buf, 0, len);
                    }
                    String responseBody = baos.toString("UTF-8");
                    if (isError) {
                        result[2] = "HTTP " + statusCode + ": " +
                                (responseBody.length() > 200 ?
                                        responseBody.substring(0, 200) : responseBody);
                        result[0] = "";
                        Log.e(TAG, "HTTP " + statusCode + " " +
                            url.substring(0, Math.min(60, url.length())) +
                            " body=" + responseBody.substring(0, Math.min(200, responseBody.length())));
                    } else {
                        result[0] = responseBody;
                        Log.i(TAG, "HTTP " + statusCode + " " +
                            url.substring(0, Math.min(60, url.length())) +
                            " body=" + responseBody.substring(0, Math.min(120, responseBody.length())));
                    }
                }
            } else if (isError) {
                result[2] = "HTTP " + statusCode + ": (no response body)";
            }

        } catch (java.net.SocketTimeoutException e) {
            result[2] = "Request timed out after " + timeoutMs + "ms: " + e.getMessage();
        } catch (Exception e) {
            result[2] = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }

        return result;
    }
}
