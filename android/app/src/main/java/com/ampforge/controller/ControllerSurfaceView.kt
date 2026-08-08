package com.ampforge.controller

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * The Controller Surface: a fixed landscape 4×2 grid of eight buttons plus a compact
 * status header. Draws the Controller Visual State vocabulary from the host contract:
 *
 *   blue active Stomp, amber bypassed Stomp, teal active Preset, dim inactive Preset,
 *   muted Section-Bypassed Button, neutral Unassigned Button.
 *
 * Colours follow the host palette (src/ToneForgeLookAndFeel.h and the ChainListBox
 * badges) so the phone matches the desktop Controller Mirror.
 */
class ControllerSurfaceView(
    context: Context,
    private val onButtonPress: (index: Int, down: Boolean) -> Unit,
) : View(context) {

    // Host palette.
    private val background = Color.rgb(0x15, 0x17, 0x1c)
    private val surface = Color.rgb(0x2a, 0x2f, 0x39)      // neutral unassigned fill
    private val surfaceRaised = Color.rgb(0x33, 0x3a, 0x45) // pressed fill
    private val outline = Color.rgb(0x3a, 0x41, 0x4d)
    private val text = Color.rgb(0xe8, 0xea, 0xed)
    private val textDim = Color.rgb(0x8b, 0x93, 0xa0)
    private val stompActive = Color.rgb(0x4a, 0x9e, 0xff)   // blue active Stomp
    private val stompBypassed = Color.rgb(0xe2, 0xb5, 0x3a) // amber bypassed Stomp
    private val presetActive = Color.rgb(0x21, 0xc0, 0x8a)  // teal active Preset
    private val warn = Color.rgb(0xe2, 0xb5, 0x3a)

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val outlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.2f)
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
    }
    private val tagPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        letterSpacing = 0.08f
        textSize = dp(9f)
    }
    private val statusPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.RIGHT
        letterSpacing = 0.06f
        textSize = dp(11f)
    }
    private val titlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        letterSpacing = 0.14f
        textSize = dp(12f)
    }

    private val buttons = Array(ControllerProtocol.NUM_BUTTONS) { ControllerProtocol.unassigned(it) }
    private var pressedIndex = -1

    var statusText: String = "Starting…"
        set(value) { field = value; postInvalidate() }
    var statusConnected: Boolean = false
        set(value) { field = value; postInvalidate() }
    var statusMismatch: Boolean = false
        set(value) { field = value; postInvalidate() }

    fun setMirror(descriptors: List<ControllerProtocol.ButtonDescriptor>) {
        for (d in descriptors) if (d.index in buttons.indices) buttons[d.index] = d
        postInvalidate()
    }

    fun updateButton(descriptor: ControllerProtocol.ButtonDescriptor) {
        if (descriptor.index in buttons.indices) {
            buttons[descriptor.index] = descriptor
            postInvalidate()
        }
    }

    fun resetMirror() {
        for (i in buttons.indices) buttons[i] = ControllerProtocol.unassigned(i)
        postInvalidate()
    }

    // ── Visual state (Controller Visual State vocabulary) ─────────────────────

    private enum class VisualState { NEUTRAL, STOMP_ACTIVE, STOMP_BYPASSED, PRESET_ACTIVE, PRESET_INACTIVE, MUTED }

    private fun visualState(d: ControllerProtocol.ButtonDescriptor): VisualState = when {
        !d.assigned -> VisualState.NEUTRAL
        d.sectionBypassed -> VisualState.MUTED
        d.isPreset -> if (d.active) VisualState.PRESET_ACTIVE else VisualState.PRESET_INACTIVE
        d.bypassed -> VisualState.STOMP_BYPASSED
        else -> VisualState.STOMP_ACTIVE
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(background)
        drawHeader(canvas)
        drawGrid(canvas)
    }

    private fun drawHeader(canvas: Canvas) {
        val headerH = dp(38f)
        titlePaint.color = textDim
        canvas.drawText("AMP FORGE CONTROLLER", dp(14f), dp(24f), titlePaint)
        statusPaint.color = when {
            statusMismatch -> warn
            statusConnected -> presetActive
            else -> textDim
        }
        val label = if (statusMismatch) "⚠ " + statusText else statusText
        canvas.drawText(label, width - dp(14f), dp(24f), statusPaint)
        canvas.drawLine(0f, headerH, width.toFloat(), headerH, outlinePaint)
    }

    private fun drawGrid(canvas: Canvas) {
        val top = dp(38f) + dp(10f)
        val gap = dp(8f)
        val cellW = (width - gap * 5) / 4f
        val cellH = (height - top - dp(10f) - gap * 3) / 2f
        val cols = 4

        for (i in buttons.indices) {
            val col = i % cols
            val row = i / cols
            val left = gap + col * (cellW + gap)
            val topC = top + row * (cellH + gap)
            drawButton(canvas, i, RectF(left, topC, left + cellW, topC + cellH))
        }
    }

    private fun drawButton(canvas: Canvas, index: Int, rect: RectF) {
        val descriptor = buttons[index]
        val state = visualState(descriptor)
        val pressed = index == pressedIndex

        when (state) {
            VisualState.NEUTRAL -> {
                fillPaint.color = if (pressed) surfaceRaised else surface
                canvas.drawRoundRect(rect, dp(10f), dp(10f), fillPaint)
                outlinePaint.color = outline
                canvas.drawRoundRect(rect, dp(10f), dp(10f), outlinePaint)
            }
            VisualState.MUTED -> {
                // Muted section-bypassed target: the underlying type colour at low alpha.
                val base = when {
                    descriptor.isPreset -> presetActive
                    descriptor.bypassed -> stompBypassed
                    else -> stompActive
                }
                fillPaint.color = withAlpha(base, if (pressed) 0.20f else 0.28f)
                canvas.drawRoundRect(rect, dp(10f), dp(10f), fillPaint)
                outlinePaint.color = withAlpha(outline, 0.5f)
                canvas.drawRoundRect(rect, dp(10f), dp(10f), outlinePaint)
            }
            VisualState.PRESET_INACTIVE -> {
                fillPaint.color = if (pressed) surfaceRaised else surface
                canvas.drawRoundRect(rect, dp(10f), dp(10f), fillPaint)
                outlinePaint.color = withAlpha(textDim, 0.55f)
                canvas.drawRoundRect(rect, dp(10f), dp(10f), outlinePaint)
            }
            else -> {
                val base = when (state) {
                    VisualState.STOMP_ACTIVE -> stompActive
                    VisualState.STOMP_BYPASSED -> stompBypassed
                    VisualState.PRESET_ACTIVE -> presetActive
                    else -> surface
                }
                fillPaint.color = if (pressed) lighten(base) else base
                canvas.drawRoundRect(rect, dp(10f), dp(10f), fillPaint)
            }
        }

        if (descriptor.assigned) {
            val centerY = rect.centerY()
            val typeTag = if (descriptor.isPreset) "PRESET" else "STOMP"
            tagPaint.color = textDim
            val tagY = rect.top + dp(20f)
            canvas.drawText(typeTag, rect.centerX(), tagY, tagPaint)

            textPaint.color = text
            textPaint.textSize = scaledLabelSize(descriptor.label, rect.width())
            val textY = (centerY + textPaint.textSize / 2f) + dp(2f)
            canvas.drawText(descriptor.label, rect.centerX(), textY, textPaint)
        } else {
            tagPaint.color = textDim
            canvas.drawText("UNASSIGNED", rect.centerX(), rect.centerY() + dp(3f), tagPaint)
        }
    }

    private fun scaledLabelSize(label: String, cellWidth: Float): Float {
        var size = dp(20f)
        val max = size * 0.55f * 2f
        val paint = Paint(textPaint).apply { textSize = size }
        val fitted = paint.breakText(label, true, cellWidth - dp(12f), null)
        if (fitted < label.length) size = (size * (fitted / label.length)).coerceAtLeast(dp(12f))
        return min(size, max)
    }

    // ── Touch: one fixed control per button, no gestures (#9) ────────────────

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val index = cellAt(event.x, event.y)
                if (index >= 0) {
                    pressedIndex = index
                    onButtonPress(index, true)
                    invalidate()
                    return true
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (pressedIndex >= 0) {
                onButtonPress(pressedIndex, false)
                pressedIndex = -1
                invalidate()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun cellAt(x: Float, y: Float): Int {
        val top = dp(38f) + dp(10f)
        val gap = dp(8f)
        val cellW = (width - gap * 5) / 4f
        val cellH = (height - top - dp(10f) - gap * 3) / 2f
        val cols = 4
        if (y < top || x < gap) return -1
        val col = ((x - gap) / (cellW + gap)).toInt()
        val row = ((y - top) / (cellH + gap)).toInt()
        if (col !in 0 until cols || row !in 0 until 2) return -1
        return row * cols + col
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun withAlpha(color: Int, alpha: Float): Int =
        Color.argb((alpha * 255).toInt().coerceIn(0, 255), Color.red(color), Color.green(color), Color.blue(color))

    private fun lighten(color: Int): Int {
        val f = 1.12f
        return Color.rgb(
            (Color.red(color) * f).toInt().coerceAtMost(255),
            (Color.green(color) * f).toInt().coerceAtMost(255),
            (Color.blue(color) * f).toInt().coerceAtMost(255),
        )
    }

    private fun dp(value: Int): Float = dp(value.toFloat())

    private fun dp(value: Float): Float = value * resources.displayMetrics.density
}
