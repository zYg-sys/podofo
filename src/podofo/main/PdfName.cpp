/**
 * SPDX-FileCopyrightText: (C) 2006 Dominik Seichter <domseichter@web.de>
 * SPDX-FileCopyrightText: (C) 2020 Francesco Pretto <ceztko@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <podofo/private/PdfDeclarationsPrivate.h>
#include "PdfName.h"

#include <podofo/private/PdfEncodingPrivate.h>

#include <podofo/auxiliary/OutputDevice.h>
#include "PdfTokenizer.h"
#include "PdfPredefinedEncoding.h"

using namespace std;
using namespace PoDoFo;

template<typename T>
void hexchr(const unsigned char ch, T& it);

static void escapeNameTo(string& dst, const bufferview& view);
static string unescapeName(const string_view& view);

const PdfName PdfName::Null = PdfName();

PdfName::PdfName() { }

PdfName::PdfName(const string& str)
{
    initFromUtf8String(str);
}

PdfName::PdfName(const string_view& view)
{
    if (view.data() == nullptr)
        PODOFO_RAISE_ERROR_INFO(PdfErrorCode::InvalidName, "Name is null");

    initFromUtf8String(view);
}

PdfName::PdfName(charbuff&& buff)
    : m_data(new NameData{ std::move(buff), nullptr, false }), m_dataView(m_data->Chars)
{
}

// NOTE: This constructor is reserved for read-only
// string literals: we just set the data view
PdfName::PdfName(const char* str, size_t length)
    : m_dataView(str, length)
{
}

void PdfName::initFromUtf8String(const char* str, size_t length)
{
    if (str == nullptr)
        PODOFO_RAISE_ERROR_INFO(PdfErrorCode::InvalidName, "Name is null");

    initFromUtf8String(string_view(str, length));
}

void PdfName::initFromUtf8String(const string_view& view)
{
    if (view.length() == 0)
    {
        // We assume it will be null name
        return;
    }

    bool isAsciiEqual;
    if (!PoDoFo::CheckValidUTF8ToPdfDocEcondingChars(view, isAsciiEqual))
        PODOFO_RAISE_ERROR_INFO(PdfErrorCode::InvalidName, "Characters in string must be PdfDocEncoding character set");

    if (isAsciiEqual)
        m_data.reset(new NameData{ charbuff(view), nullptr, true });
    else
        m_data.reset(new NameData{ (charbuff)PoDoFo::ConvertUTF8ToPdfDocEncoding(view), std::make_unique<string>(view), true });

    m_dataView = m_data->Chars;
}

PdfName PdfName::FromEscaped(const string_view& view)
{
    return FromRaw(unescapeName(view));
}

PdfName PdfName::FromRaw(const bufferview& rawcontent)
{
    return PdfName((charbuff)rawcontent);
}

void PdfName::Write(OutputStream& device, PdfWriteFlags,
    const PdfStatefulEncrypt* encrypt, charbuff& buffer) const
{
    (void)encrypt;
    // Allow empty names, which are legal according to the PDF specification
    device.Write('/');
    if (m_dataView.size() != 0)
    {
        escapeNameTo(buffer, m_dataView);
        device.Write(buffer);
    }
}

string PdfName::GetEscapedName() const
{
    if (m_dataView.size() == 0)
        return string();

    string ret;
    escapeNameTo(ret, m_dataView);
    return ret;
}

void PdfName::expandUtf8String()
{
    PODOFO_INVARIANT(m_data != nullptr);
    if (!m_data->IsUtf8Expanded)
    {
        bool isAsciiEqual;
        string utf8str;
        PoDoFo::ConvertPdfDocEncodingToUTF8(m_data->Chars, utf8str, isAsciiEqual);
        if (!isAsciiEqual)
            m_data->Utf8String.reset(new string(std::move(utf8str)));

        m_data->IsUtf8Expanded = true;
    }
}

/** Escape the input string according to the PDF name
 *  escaping rules and return the result.
 *
 *  \param it Iterator referring to the start of the input string
 *            ( eg a `const char *' or a `std::string::iterator' )
 *  \param length Length of input string
 *  \returns Escaped string
 */
void escapeNameTo(string& dst, const bufferview& view)
{
    // Scan the input string once to find out how much memory we need
    // to reserve for the encoded result string. We could do this in one
    // pass using a ostringstream instead, but it's a LOT slower.
    size_t outchars = 0;
    for (size_t i = 0; i < view.size(); i++)
    {
        char ch = view[i];

        // Null chars are illegal in names, even escaped
        if (ch == '\0')
        {
            PODOFO_RAISE_ERROR_INFO(PdfErrorCode::InvalidName, "Null byte in PDF name is illegal");
        }
        else
        {
            // Leave room for either just the char, or a #xx escape of it.
            outchars += (PdfTokenizer::IsRegular(ch) &&
                PdfTokenizer::IsPrintable(ch) && (ch != '#')) ? 1 : 3;
        }
    }
    // Reserve it. We can't use reserve() because the GNU STL doesn't seem to
    // do it correctly; the memory never seems to get allocated.
    dst.resize(outchars);
    // and generate the encoded string
    string::iterator bufIt(dst.begin());
    for (size_t i = 0; i < view.size(); i++)
    {
        char ch = view[i];
        if (PdfTokenizer::IsRegular(ch)
            && PdfTokenizer::IsPrintable(ch)
            && ch != '#')
        {
            *(bufIt++) = ch;
        }
        else
        {
            *(bufIt++) = '#';
            hexchr(static_cast<unsigned char>(ch), bufIt);
        }
    }
}

/** Interpret the passed string as an escaped PDF name
 *  and return the unescaped form.
 *
 *  \param it Iterator referring to the start of the input string
 *            ( eg a `const char *' or a `std::string::iterator' )
 *  \param length Length of input string
 *  \returns Unescaped string
 */
string unescapeName(const string_view& view)
{
    // We know the decoded string can be AT MOST
    // the same length as the encoded one, so:
    string ret;
    ret.reserve(view.length());
    size_t incount = 0;
    const char* curr = view.data();
    while (incount++ < view.length())
    {
        if (*curr == '#' && incount + 1 < view.length())
        {
            unsigned char hi = static_cast<unsigned char>(*(++curr));
            incount++;
            unsigned char low = static_cast<unsigned char>(*(++curr));
            incount++;
            hi -= (hi < 'A' ? '0' : 'A' - 10);
            low -= (low < 'A' ? '0' : 'A' - 10);
            unsigned char codepoint = (hi << 4) | (low & 0x0F);
            ret.push_back((char)codepoint);
        }
        else
            ret.push_back(*curr);

        curr++;
    }

    return ret;
}

string_view PdfName::GetString() const
{
    if (m_data == nullptr)
    {
        // This was name was constructed from a read-only string literal
        return m_dataView;
    }
    else
    {
        const_cast<PdfName&>(*this).expandUtf8String();
        if (m_data->Utf8String == nullptr)
            return m_data->Chars;
        else
            return *m_data->Utf8String;
    }
}

bool PdfName::IsNull() const
{
    PODOFO_INVARIANT(m_dataView.size() != 0 || m_dataView.data() == nullptr);
    return m_dataView.size() == 0;
}

std::string_view PdfName::GetRawData() const
{
    return m_dataView;
}

bool PdfName::operator==(const PdfName& rhs) const
{
    return this->m_dataView == rhs.m_dataView;
}

bool PdfName::operator!=(const PdfName& rhs) const
{
    return this->m_dataView != rhs.m_dataView;
}

bool PdfName::operator==(const char* str) const
{
    return operator==(string_view(str, std::strlen(str)));
}

bool PdfName::operator==(const string& str) const
{
    return operator==((string_view)str);
}

bool PdfName::operator==(const string_view& view) const
{
    return GetString() == view;
}

bool PdfName::operator!=(const char* str) const
{
    return operator!=(string_view(str, std::strlen(str)));
}

bool PdfName::operator!=(const string& str) const
{
    return operator!=((string_view)str);
}

bool PdfName::operator!=(const string_view& view) const
{
    return GetString() != view;
}

bool PdfName::operator<(const PdfName& rhs) const
{
    return this->m_dataView < rhs.m_dataView;
}

PdfName::operator string_view() const
{
    return m_dataView;
}

/**
 * This function writes a hex encoded representation of the character
 * `ch' to `buf', advancing the iterator by two steps.
 *
 * \warning no buffer length checking is performed, so MAKE SURE
 *          you have enough room for the two characters that
 *          will be written to the buffer.
 *
 * \param ch The character to write a hex representation of
 * \param buf An iterator (eg a char* or std::string::iterator) to write the
 *            characters to.  Must support the postfix ++, operator=(char) and
 *            dereference operators.
 */
template<typename T>
void hexchr(const unsigned char ch, T& it)
{
    *(it++) = "0123456789ABCDEF"[ch / 16];
    *(it++) = "0123456789ABCDEF"[ch % 16];
}
