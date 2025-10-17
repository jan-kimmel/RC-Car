package com.example.bluetoothcontroller;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {
    private static final int MAX_LOG_LINES = 200; // Oberfläche Anzeigebegrenzung
    private final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB"); // Konstante ID für SPP-Bluetooth

    // letzte Trigger- und Stickpositionen
    private int lastLT = 0; // linker Trigger
    private int lastRT = 0; // rechter Trigger
    private int lastLX = 0; // linker Stick

    private TextView controllerDataText; // Oberfläche Liste der Controller-Inputs

    // Bluetoothsteuerung
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket bluetoothSocket;
    private BluetoothManager bluetoothManager;
    private OutputStream outputStream;

    private final ActivityResultLauncher<String[]> permissionLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestMultiplePermissions(), result -> {
                Boolean connectGranted = result.getOrDefault(Manifest.permission.BLUETOOTH_CONNECT, false);

                if (Boolean.TRUE.equals(connectGranted)) {
                    connectToPairedHC05();
                } else {
                    Toast.makeText(this, "Bluetoothberechtigung abgelehnt", Toast.LENGTH_SHORT).show();
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        controllerDataText = findViewById(R.id.body_text);

        bluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissionLauncher.launch(new String[]{Manifest.permission.BLUETOOTH_CONNECT});
        } else {
            connectToPairedHC05();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        try {
            if (outputStream != null) outputStream.close();
            if (bluetoothSocket != null) bluetoothSocket.close();
        } catch (IOException e) {
            Log.e("Bluetooth", "Fehler beim Schließen", e);
        }
    }

    private void connectToPairedHC05() {
        Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();

        for (BluetoothDevice device : pairedDevices) { // gepaarte Geräte durchlaufen
            if (device.getName() != null && device.getName().contains("HC-05")) { // nach HC-05 suchen

                new Thread(() -> {
                    try {
                        bluetoothSocket = device.createRfcommSocketToServiceRecord(SPP_UUID);
                        bluetoothSocket.connect();
                        outputStream = bluetoothSocket.getOutputStream();

                        runOnUiThread(() -> {
                            Toast.makeText(this, "Bluetooth verbunden!", Toast.LENGTH_SHORT).show();
                        });
                    } catch (IOException e) {
                        Log.e("Bluetooth", "Verbindung fehlgeschlagen!", e);
                        runOnUiThread(() -> {
                            Toast.makeText(this, "Bluetooth-Verbindung fehlgeschlagen", Toast.LENGTH_SHORT).show();
                        });
                    }
                }).start();
                return;
            }
        }

        Toast.makeText(this, "Kein HC-05 gefunden!", Toast.LENGTH_SHORT).show();
    }

    @Override // Digitale Inputs verarbeiten
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        boolean isGamePad = isGamePadEvent(event);
        if (isGamePad) {
            String msg; // = KeyEvent.keyCodeToString(keyCode);
            switch (KeyEvent.keyCodeToString(keyCode)) {
                case "KEYCODE_BUTTON_A": msg = "A"; break;
                case "KEYCODE_BUTTON_B": msg = "B"; break;
                case "KEYCODE_BUTTON_X": msg = "X"; break;
                case "KEYCODE_BUTTON_Y": msg = "Y"; break;
                case "KEYCODE_BUTTON_L1": msg = "L"; break;
                case "KEYCODE_BUTTON_R1": msg = "R"; break;
                default: msg = "";
            }
            sendMsg(msg);
        }
        return isGamePad || super.onKeyDown(keyCode, event);
    }

    @Override // Digitale Inputs verarbeiten (X-Knopf loslassen)
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        boolean isGamePad = isGamePadEvent(event);
        if (isGamePad && keyCode == KeyEvent.KEYCODE_BUTTON_X) {
            sendMsg("Z");
        }
        return isGamePad || super.onKeyDown(keyCode, event);
    }

    @Override // Analoge Inputs verarbeiten
    public boolean onGenericMotionEvent(MotionEvent event) {
        boolean isGamePad = isGamePadEvent(event);
        if (isGamePad) {
            int lT = Math.round(event.getAxisValue(MotionEvent.AXIS_LTRIGGER)*255); // Linker Trigger (0.0 - 1.0 -> 0 - 255)
            int rT = Math.round(event.getAxisValue(MotionEvent.AXIS_RTRIGGER)*255); // Rechter Trigger (0.0 - 1.0 -> 0 - 255)
            int lX = (Math.round(event.getAxisValue(MotionEvent.AXIS_X)*255)+255)/2; // Linker Stick X_Achse (-1.0 - 1.0 -> 0 - 255)
            float dX = event.getAxisValue(MotionEvent.AXIS_HAT_X); // D-Pad links/rechts
            float dY = event.getAxisValue(MotionEvent.AXIS_HAT_Y); // D-Pad oben/unten

            if (lT - lastLT != 0) {
                sendMsg("LT" + String.format("%02X", lT));
                lastLT = lT;
            }
            if (rT - lastRT != 0) {
                sendMsg("RT" + String.format("%02X", rT));
                lastRT = rT;
            }
            if (lX - lastLX != 0) {
                sendMsg("LX" + String.format("%02X", lX));
                lastLX = lX;
            }
            if (dX == -1f) {
                sendMsg("l");
            }
            if (dX == 1f) {
                sendMsg("r");
            }
            if (dY == -1f) {
                sendMsg("u");
            }
            if (dY == 1f) {
                sendMsg("d");
            }
        }
        return isGamePad || super.onGenericMotionEvent(event);
    }

    private boolean isGamePadEvent(InputEvent event) {
        int source = event.getSource();
        return ((source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) ||
                ((source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD);
    }

    public void sendMsg(String msg) {
        if (msg != null && msg != "") {
            msg += System.lineSeparator();

            // msg an HC-05 senden
            if (bluetoothSocket != null && bluetoothSocket.isConnected() && outputStream != null) {
                try {
                    outputStream.write(msg.getBytes());
                } catch (IOException e) {
                    Log.e("Bluetooth", "Senden fehlgeschlagen", e);
                }
            }

            // Daten auf Oberfläche anzeigen
            controllerDataText.append(msg);

            String text = controllerDataText.getText().toString();
            String[] lines = text.split("\n");

            // Textfeld begrenzen
            if (lines.length > MAX_LOG_LINES) {
                StringBuilder newText = new StringBuilder();
                for (int i = lines.length - MAX_LOG_LINES; i < lines.length; i++) {
                    newText.append(lines[i]).append("\n");
                }
                controllerDataText.setText(newText.toString());
            }

            // Text weiter scrollen
            ScrollView scrollView = findViewById(R.id.scroll_view);
            scrollView.post(() -> scrollView.fullScroll(ScrollView.FOCUS_DOWN));
        }
    }
}