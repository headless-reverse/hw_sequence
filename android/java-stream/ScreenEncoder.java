package dev.headless.sequence;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.view.Surface;
import android.os.IBinder;
import java.io.DataOutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;
import dev.headless.sequence.Server;
import dev.headless.sequence.SurfaceControl;
import dev.headless.sequence.MetaInfo;

public class ScreenEncoder {
    private MediaCodec encoder;
    private final int baseWidth;
    private final int baseHeight;
    private final int bitrate;
    
    private static final byte PACKET_TYPE_VIDEO = 1;

    public ScreenEncoder(int width, int height, int bitrate) {
        this.baseWidth = width;
        this.baseHeight = height;
        this.bitrate = bitrate;
    }

    public void stream(DataOutputStream out) throws Exception {
        WritableByteChannel channel = Channels.newChannel(out);
        while (!Thread.interrupted()) {
            int rotation = MetaInfo.getRotation();
            boolean isLandscape = (rotation == 90 || rotation == 270);
            int rawW = isLandscape ? Math.max(baseWidth, baseHeight) : Math.min(baseWidth, baseHeight);
            int rawH = isLandscape ? Math.min(baseWidth, baseHeight) : Math.max(baseWidth, baseHeight);
            int w = (rawW + 15) & ~15;
            int h = (rawH + 15) & ~15;
            System.out.println(String.format("Encoder init: %dx%d (rot: %d)", w, h, rotation));
            MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, w, h);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, 60);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            format.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 1000000 / 30);
            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            try {
                encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
                Surface inputSurface = encoder.createInputSurface();
                IBinder displayToken = Server.createDisplayMirror(inputSurface, w, h);
                encoder.start();
                try {
                    MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
                    while (!Thread.interrupted()) {
                        if (MetaInfo.getRotation() != rotation) {
                            System.out.println("Rotation change detected, restarting stream...");
                            break; 
                        }
                        int outputBufferIndex = encoder.dequeueOutputBuffer(bufferInfo, 10000);
                        if (outputBufferIndex >= 0) {
                            ByteBuffer buf = encoder.getOutputBuffer(outputBufferIndex);
                            if (buf != null && bufferInfo.size > 0) {
                                out.writeByte(PACKET_TYPE_VIDEO);
                                out.writeInt(bufferInfo.size);
                                buf.position(bufferInfo.offset);
                                buf.limit(bufferInfo.offset + bufferInfo.size);
                                channel.write(buf);
                                out.flush();
                            }
                            encoder.releaseOutputBuffer(outputBufferIndex, false);
                        }
                    }
                } finally {
                    destroyDisplay(displayToken);
                    cleanup(inputSurface);
                }
            } catch (Exception e) {
                System.err.println("Critical encoder error for " + w + "x" + h + ": " + e.getMessage());
                throw e;
            }
        }
    }

    private void destroyDisplay(IBinder token) {
        if (token != null) {
            try {
                Class.forName("android.view.SurfaceControl")
                     .getMethod("destroyDisplay", IBinder.class).invoke(null, token);
            } catch (Exception e) {
                System.err.println("Failed to destroy display: " + e.getMessage());
            }
        }
    }

    private void cleanup(Surface inputSurface) {
        try {
            if (encoder != null) {
                encoder.stop();
                encoder.release();
            }
            if (inputSurface != null) {
                inputSurface.release();
            }
        } catch (Exception ignored) {}
    }
}
