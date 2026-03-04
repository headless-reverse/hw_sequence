package dev.headless.sequence;

import android.os.IBinder;
import android.view.Surface;
import android.graphics.Rect;
import java.io.*;
import java.net.ServerSocket;
import java.net.Socket;
import java.lang.reflect.Method;
import org.json.JSONObject;

public class Server {

    public static void main(String[] args) {
        try {
            int port = 7373;
            if (args.length > 0) {
                port = Integer.parseInt(args[0]);}
            new Server().start(port);
        } catch (Exception e) {
            System.err.println("Fatal error: " + e.toString());
            e.printStackTrace();
            System.exit(1);}}

    public void start(int port) throws Exception {
        try (ServerSocket serverSocket = new ServerSocket(port)) {
            System.out.println("adb_sequence_server: Listening on port " + port);
            while (true) {
                Socket client = serverSocket.accept();
                new Thread(() -> handleClient(client)).start();}}}

    private void handleClient(Socket client) {
        System.out.println("client connected: " + client.getRemoteSocketAddress());
        try {
            client.setTcpNoDelay(true);
            DataInputStream in = new DataInputStream(client.getInputStream());
            DataOutputStream out = new DataOutputStream(client.getOutputStream());
            int targetWidth = 720;
            int targetHeight = 1280;
            int bitrate = 4000000;
            try {
                client.setSoTimeout(2000); 
                targetWidth = in.readInt();
                targetHeight = in.readInt();
                bitrate = in.readInt();
                if (targetWidth <= 0) targetWidth = 720;
                if (targetHeight <= 0) targetHeight = 1280;
            } catch (IOException e) {
                System.out.println("Handshake failed or timed out. Using default 720x1280.");
            }
            client.setSoTimeout(0);
            int alignedW = (targetWidth + 15) & ~15;
            int alignedH = (targetHeight + 15) & ~15;
            try {
                JSONObject meta = new JSONObject();
                meta.put("w", alignedW);
                meta.put("h", alignedH);
                int rot = MetaInfo.getRotation();
                meta.put("rot", rot);
                byte[] metaBytes = meta.toString().getBytes("UTF-8");
                out.writeByte(Protocol.TYPE_META);
                out.writeInt(metaBytes.length);
                out.write(metaBytes);
                out.flush();
                System.out.println("Sent META packet (aligned): " + meta.toString());
            } catch (Exception me) {
                System.err.println("Failed to send META packet: " + me.toString());
            }
            ScreenEncoder encoder = new ScreenEncoder(targetWidth, targetHeight, bitrate);
            encoder.stream(out);
        } catch (Exception e) {
            System.err.println("serverERROR: " + e.toString());
            e.printStackTrace();
        } finally {
            try {
                client.close();
                System.out.println("client connection closed.");
            } catch (IOException e) {}
        }
    }

	public static IBinder createDisplayMirror(Surface surface, int w, int h) throws Exception {
		IBinder display = SurfaceControl.getBuiltInDisplay();
		if (display == null) throw new RuntimeException("No display token!");
		IBinder virtualDisplay = SurfaceControl.createDisplay("adb_sequence", false);
		SurfaceControl.openTransaction();
		try {
			SurfaceControl.setDisplaySurface(virtualDisplay, surface);
			Rect rect = new Rect(0, 0, w, h);
			SurfaceControl.setDisplayProjection(virtualDisplay, 0, rect, rect);
			SurfaceControl.setDisplayLayerStack(virtualDisplay, 0);
		} finally {
			SurfaceControl.closeTransaction();
		}
		return virtualDisplay;
	}

    private static IBinder getBuiltInDisplay() throws Exception {try {
            return (IBinder) Class.forName("android.view.SurfaceControl")
                    .getMethod("getInternalDisplayToken").invoke(null);} catch (Exception e) {
            return (IBinder) Class.forName("android.view.SurfaceControl")
                    .getMethod("getBuiltInDisplay", int.class).invoke(null, 0);}}

    private static void openTransaction() throws Exception {
        Class.forName("android.view.SurfaceControl").getMethod("openTransaction").invoke(null);}

    private static void closeTransaction() throws Exception {
        Class.forName("android.view.SurfaceControl").getMethod("closeTransaction").invoke(null);}

    private static void setDisplaySurface(IBinder display, Surface surface) throws Exception {
        Class.forName("android.view.SurfaceControl")
                .getMethod("setDisplaySurface", IBinder.class, Surface.class)
                .invoke(null, display, surface);}

    private static void setDisplayProjection(IBinder display, int orientation, Rect layerStack, Rect displayRect) throws Exception {
        Class.forName("android.view.SurfaceControl")
                .getMethod("setDisplayProjection", IBinder.class, int.class, Rect.class, Rect.class)
                .invoke(null, display, orientation, layerStack, displayRect);}

    private static void setDisplayLayerStack(IBinder display, int layerStack) throws Exception {
        Class.forName("android.view.SurfaceControl")
                .getMethod("setDisplayLayerStack", IBinder.class, int.class)
                .invoke(null, display, layerStack);}}
