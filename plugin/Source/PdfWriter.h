#pragma once
// Minimal PDF 1.4 writer: A4 pages, standard Type1 fonts (Helvetica,
// Helvetica-Bold, Courier), filled rectangles and JPEG images. Enough for a
// clean engineering document without external dependencies.

#include <JuceHeader.h>

class PdfWriter
{
public:
    enum FontId { helvetica = 0, helveticaBold, courier };
    static constexpr float pageW = 595.0f, pageH = 842.0f, margin = 50.0f;

    void newPage()
    {
        pages.add ({});
        cursorY = margin;
    }

    static juce::String sanitise (juce::String s)
    {
        s = s.replace (juce::CharPointer_UTF8 ("\xc2\xb1"), "+/-").replace (juce::CharPointer_UTF8 ("\xc2\xb0"), " deg")
             .replace (juce::CharPointer_UTF8 ("\xe2\x80\x94"), "-").replace (juce::CharPointer_UTF8 ("\xe2\x80\x93"), "-")
             .replace (juce::CharPointer_UTF8 ("\xe2\x86\x92"), "->").replace (juce::CharPointer_UTF8 ("\xc3\x97"), "x");
        juce::String out;
        for (auto c : s)
        {
            if (c == '\\' || c == '(' || c == ')') { out << '\\' << juce::String::charToString (c); }
            else if (c >= 32 && c < 127) out << juce::String::charToString (c);
            else if (c == '\t') out << "    ";
            else out << "?";
        }
        return out;
    }

    void text (float x, float y, const juce::String& s, float size, FontId f = helvetica, juce::Colour c = juce::Colours::black)
    {
        ensurePage();
        pages.getReference (pages.size() - 1)
            << colour (c, false) << "BT /F" << (int) f << " " << num (size) << " Tf " << num (x) << " " << num (pageH - y) << " Td ("
            << sanitise (s) << ") Tj ET\n";
    }

    // Word-wrapped paragraph at the cursor. Returns the height used.
    void paragraph (const juce::String& s, float size = 10.0f, FontId f = helvetica, juce::Colour c = juce::Colours::black,
                    float indent = 0.0f, float spaceAfter = 6.0f)
    {
        const float width = pageW - 2 * margin - indent;
        const float charW = size * (f == courier ? 0.6f : 0.5f);
        const int maxChars = juce::jmax (10, (int) (width / charW));
        for (auto& rawLine : juce::StringArray::fromLines (s))
        {
            juce::String line;
            for (auto& word : juce::StringArray::fromTokens (rawLine, " ", ""))
            {
                if (line.length() + word.length() + 1 > maxChars && line.isNotEmpty())
                {
                    emitLine (line, size, f, c, indent);
                    line.clear();
                }
                line << (line.isEmpty() ? "" : " ") << word;
            }
            emitLine (line, size, f, c, indent);
        }
        cursorY += spaceAfter;
    }

    void heading (const juce::String& s, float size = 15.0f)
    {
        space (size * 2.2f);
        cursorY += size * 0.6f;
        text (margin, cursorY + size * 0.4f, s, size, helveticaBold, juce::Colour (0xff1c2633));
        cursorY += size * 0.9f;
        rect (margin, cursorY, pageW - 2 * margin, 0.6f, juce::Colour (0xffb9a27c));
        cursorY += 8.0f;
    }

    void rect (float x, float y, float w, float h, juce::Colour c)
    {
        ensurePage();
        pages.getReference (pages.size() - 1) << colour (c, true) << num (x) << " " << num (pageH - y - h) << " " << num (w) << " " << num (h) << " re f\n";
    }

    void image (const juce::Image& img, float w)
    {
        const float h = w * (float) img.getHeight() / (float) img.getWidth();
        space (h + 10.0f);
        juce::MemoryOutputStream jpeg;
        juce::JPEGImageFormat fmt;
        fmt.setQuality (0.9f);
        fmt.writeImageToStream (img, jpeg);
        images.add ({ jpeg.getMemoryBlock(), img.getWidth(), img.getHeight() });
        pages.getReference (pages.size() - 1) << "q " << num (w) << " 0 0 " << num (h) << " " << num (margin) << " "
                                              << num (pageH - cursorY - h) << " cm /Im" << images.size() << " Do Q\n";
        cursorY += h + 10.0f;
    }

    void space (float needed)
    {
        ensurePage();
        if (cursorY + needed > pageH - margin)
            newPage();
    }

    float cursorY = margin;

    bool save (const juce::File& file)
    {
        juce::MemoryOutputStream out;
        juce::Array<juce::int64> offsets;
        auto obj = [&] (const juce::String& body)
        {
            offsets.add ((juce::int64) out.getPosition());
            out << juce::String (offsets.size()) << " 0 obj\n" << body << "\nendobj\n";
        };
        out << "%PDF-1.4\n";
        const int numPages = pages.size();
        // object numbering: 1 catalog, 2 pages, 3..5 fonts, then images, then (page, content) pairs
        const int firstImage = 6, firstPage = firstImage + images.size();
        obj ("<< /Type /Catalog /Pages 2 0 R >>");
        juce::String kids;
        for (int i = 0; i < numPages; ++i) kids << (firstPage + i * 2) << " 0 R ";
        obj ("<< /Type /Pages /Kids [" + kids + "] /Count " + juce::String (numPages) + " >>");
        obj ("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        obj ("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
        obj ("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
        for (int i = 0; i < images.size(); ++i)
        {
            auto& im = images.getReference (i);
            offsets.add ((juce::int64) out.getPosition());
            out << juce::String (offsets.size()) << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << im.w << " /Height " << im.h
                << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << (int) im.data.getSize() << " >>\nstream\n";
            out.write (im.data.getData(), im.data.getSize());
            out << "\nendstream\nendobj\n";
        }
        juce::String xobjects;
        for (int i = 0; i < images.size(); ++i) xobjects << "/Im" << (i + 1) << " " << (firstImage + i) << " 0 R ";
        for (int p = 0; p < numPages; ++p)
        {
            obj ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F0 3 0 R /F1 4 0 R /F2 5 0 R >> /XObject << "
                 + xobjects + ">> >> /Contents " + juce::String (firstPage + p * 2 + 1) + " 0 R >>");
            const auto content = pages[p].toStdString();
            offsets.add ((juce::int64) out.getPosition());
            out << juce::String (offsets.size()) << " 0 obj\n<< /Length " << (int) content.size() << " >>\nstream\n";
            out.write (content.data(), content.size());
            out << "\nendstream\nendobj\n";
        }
        const auto xref = out.getPosition();
        out << "xref\n0 " << (offsets.size() + 1) << "\n0000000000 65535 f \n";
        for (auto o : offsets)
            out << juce::String (o).paddedLeft ('0', 10) << " 00000 n \n";
        out << "trailer\n<< /Size " << (offsets.size() + 1) << " /Root 1 0 R >>\nstartxref\n" << juce::String ((juce::int64) xref) << "\n%%EOF\n";
        return file.replaceWithData (out.getData(), out.getDataSize());
    }

private:
    struct Img { juce::MemoryBlock data; int w, h; };

    void ensurePage() { if (pages.isEmpty()) newPage(); }

    void emitLine (const juce::String& line, float size, FontId f, juce::Colour c, float indent)
    {
        space (size * 1.4f);
        text (margin + indent, cursorY + size, line, size, f, c);
        cursorY += size * 1.4f;
    }

    static juce::String num (float v) { return juce::String (v, 2); }
    static juce::String colour (juce::Colour c, bool fill)
    {
        return juce::String (c.getFloatRed(), 3) + " " + juce::String (c.getFloatGreen(), 3) + " " + juce::String (c.getFloatBlue(), 3)
               + (fill ? " rg " : " rg ");
    }

    juce::StringArray pages;
    juce::Array<Img> images;
};
