#include "Media.h"

#include "Json.h"

#include <objidl.h>
#include <winhttp.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace ncmmini
{
namespace
{
constexpr std::uintmax_t MaximumCacheFileSize = 16 * 1024 * 1024;
constexpr std::size_t CoverWidth = 40;
constexpr std::size_t CoverHeight = 40;

template <typename Interface>
void Release(Interface*& value)
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

bool ReadFile(const std::filesystem::path& path, std::string& text)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > MaximumCacheFileSize)
    {
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }
    return !text.empty();
}

std::wstring JsonText(const JsonValue* value)
{
    if (value == nullptr)
    {
        return {};
    }
    if (const auto* text = value->AsString())
    {
        return Utf8ToWide(*text);
    }
    if (const auto* number = value->AsNumber())
    {
        if (!std::isfinite(*number))
        {
            return {};
        }
        std::wostringstream stream;
        if (std::floor(*number) == *number)
        {
            stream << std::fixed << std::setprecision(0) << *number;
        }
        else
        {
            stream << std::setprecision(std::numeric_limits<double>::max_digits10) << *number;
        }
        return stream.str();
    }
    return {};
}

std::wstring FirstText(const JsonValue& object, std::initializer_list<const char*> names)
{
    for (const auto* name : names)
    {
        auto value = JsonText(object.Find(name));
        if (!Trim(value).empty())
        {
            return value;
        }
    }
    return {};
}

std::wstring ReadArtists(const JsonValue& object)
{
    const JsonValue* artists = object.Find("artists");
    if (artists == nullptr)
    {
        artists = object.Find("ar");
    }
    if (artists == nullptr)
    {
        return JsonText(object.Find("artist"));
    }
    if (const auto* direct = artists->AsString())
    {
        return Utf8ToWide(*direct);
    }
    const auto* array = artists->AsArray();
    if (array == nullptr)
    {
        return {};
    }
    std::wstring result;
    for (const auto& item : *array)
    {
        auto name = item.AsObject() == nullptr ? JsonText(&item) : JsonText(item.Find("name"));
        if (!Trim(name).empty())
        {
            if (!result.empty()) result += L"/";
            result += name;
        }
    }
    return result;
}

TrackInfo TryCreateTrack(const JsonValue& value)
{
    TrackInfo track;
    if (value.AsObject() == nullptr)
    {
        return track;
    }
    track.name = JsonText(value.Find("name"));
    if (Trim(track.name).empty())
    {
        return {};
    }
    track.artist = ReadArtists(value);
    const JsonValue* album = value.Find("album");
    if (album == nullptr || album->AsObject() == nullptr)
    {
        album = value.Find("al");
    }
    track.coverUrl = album != nullptr && album->AsObject() != nullptr
        ? FirstText(*album, {"picUrl", "coverImgUrl", "coverUrl", "blurPicUrl"})
        : FirstText(value, {"picUrl", "coverImgUrl", "coverUrl", "blurPicUrl"});
    track.trackId = FirstText(value, {"id", "trackId"});
    track.lyricsId = FirstText(value, {"lrcid", "lyricsId", "lyricId"});
    return track;
}

void VisitTracks(const JsonValue& value, std::map<std::wstring, TrackInfo>& tracks)
{
    if (const auto* object = value.AsObject())
    {
        auto track = TryCreateTrack(value);
        if (!track.name.empty())
        {
            auto key = track.trackId.empty() ? track.name + std::wstring(1, L'\0') + track.artist : track.trackId;
            tracks[key] = std::move(track);
        }
        for (const auto& property : *object)
        {
            VisitTracks(property.second, tracks);
        }
    }
    else if (const auto* array = value.AsArray())
    {
        for (const auto& item : *array)
        {
            VisitTracks(item, tracks);
        }
    }
}

std::wstring Normalize(const std::wstring& value)
{
    std::wstring result;
    bool pendingSpace = false;
    for (const auto character : Trim(value))
    {
        if (iswspace(character))
        {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace)
        {
            result.push_back(L' ');
            pendingSpace = false;
        }
        result.push_back(static_cast<wchar_t>(towlower(character)));
    }
    return result;
}

bool ContainsCaseInsensitive(const std::wstring& text, const std::wstring& fragment)
{
    return Normalize(text).find(Normalize(fragment)) != std::wstring::npos;
}

std::wstring NormalizeCoverUrl(std::wstring url)
{
    url = Trim(std::move(url));
    if (url.rfind(L"//", 0) == 0)
    {
        return L"https:" + url;
    }
    if (url.size() >= 7 && _wcsnicmp(url.c_str(), L"http://", 7) == 0)
    {
        return L"https://" + url.substr(7);
    }
    return url;
}

std::vector<std::uint8_t> Download(const std::wstring& sourceUrl)
{
    const auto url = NormalizeCoverUrl(sourceUrl);
    if (url.empty())
    {
        return {};
    }
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components))
    {
        return {};
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0)
    {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(L"NCM-Mini/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        return {};
    }
    WinHttpSetTimeouts(session, 4000, 4000, 8000, 8000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    std::vector<std::uint8_t> result;
    if (request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr))
    {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status >= 200 && status < 300)
        {
            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                if (result.size() + available > 8 * 1024 * 1024)
                {
                    result.clear();
                    break;
                }
                const auto offset = result.size();
                result.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, result.data() + offset, available, &read))
                {
                    result.clear();
                    break;
                }
                result.resize(offset + read);
            }
        }
    }
    if (request != nullptr) WinHttpCloseHandle(request);
    if (connection != nullptr) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

std::vector<std::uint8_t> DecodeCover(std::vector<std::uint8_t>& encoded)
{
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
    {
        return {};
    }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<std::uint8_t> pixels;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
    if (SUCCEEDED(result)) result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(result)) result = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(result)) result = scaler->Initialize(frame, CoverWidth, CoverHeight, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) result = converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(result))
    {
        pixels.resize(CoverWidth * CoverHeight * 4);
        result = converter->CopyPixels(nullptr, CoverWidth * 4, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(result)) pixels.clear();
    }
    Release(converter);
    Release(scaler);
    Release(frame);
    Release(decoder);
    Release(stream);
    Release(factory);
    return pixels;
}
}

TrackInfo TrackCatalog::Find(const std::wstring& title)
{
    const auto [parsedName, parsedArtist] = ParsePlayerTitle(title);
    if (Trim(parsedName).empty())
    {
        return {};
    }
    EnsureLoaded();
    const auto name = Normalize(parsedName);
    const auto artist = Normalize(parsedArtist);
    const TrackInfo* best = nullptr;
    int bestScore = -1;
    for (const auto& track : tracks_)
    {
        if (Normalize(track.name) != name)
        {
            continue;
        }
        const int score = !artist.empty() && ContainsCaseInsensitive(track.artist, artist) ? 1 : 0;
        if (score > bestScore)
        {
            best = &track;
            bestScore = score;
        }
    }
    return best == nullptr ? TrackInfo{} : *best;
}

void TrackCatalog::EnsureLoaded()
{
    const auto now = std::chrono::steady_clock::now();
    if (lastLoad_.time_since_epoch().count() != 0 && now - lastLoad_ < std::chrono::seconds(5))
    {
        return;
    }
    lastLoad_ = now;
    std::error_code error;
    if (!std::filesystem::exists(dataDirectory_, error))
    {
        tracks_.clear();
        return;
    }
    struct Candidate
    {
        std::filesystem::file_time_type time;
        std::filesystem::path path;
    };
    std::vector<Candidate> candidates;
    std::filesystem::recursive_directory_iterator iterator(dataDirectory_,
        std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error)) continue;
        const auto size = iterator->file_size(error);
        if (error || size == 0 || size > MaximumCacheFileSize)
        {
            error.clear();
            continue;
        }
        candidates.push_back({iterator->last_write_time(error), iterator->path()});
        error.clear();
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) { return left.time > right.time; });
    if (candidates.size() > 96) candidates.resize(96);

    std::map<std::wstring, TrackInfo> tracks;
    for (const auto& candidate : candidates)
    {
        std::string text;
        JsonValue root;
        if (ReadFile(candidate.path, text) && ParseJson(text, root))
        {
            VisitTracks(root, tracks);
        }
    }
    tracks_.clear();
    tracks_.reserve(tracks.size());
    for (auto& [key, track] : tracks)
    {
        tracks_.push_back(std::move(track));
    }
}

std::vector<LyricLine> LyricsStore::Find(const TrackInfo& track) const
{
    std::vector<std::wstring> identifiers;
    if (!Trim(track.lyricsId).empty()) identifiers.push_back(track.lyricsId);
    if (!Trim(track.trackId).empty()) identifiers.push_back(track.trackId);
    if (identifiers.empty())
    {
        return {};
    }
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(dataDirectory_,
        std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    std::size_t inspected = 0;
    for (; !error && iterator != end && inspected < 32; iterator.increment(error))
    {
        if (!iterator->is_regular_file(error)) continue;
        const auto filename = iterator->path().filename().wstring();
        if (!std::any_of(identifiers.begin(), identifiers.end(), [&](const auto& id) { return ContainsCaseInsensitive(filename, id); }))
        {
            continue;
        }
        ++inspected;
        std::string text;
        if (ReadFile(iterator->path(), text))
        {
            auto lines = Parse(text);
            if (!lines.empty()) return lines;
        }
    }
    return {};
}

std::vector<LyricLine> LyricsStore::Parse(const std::string& text)
{
    std::vector<LyricLine> lines;
    std::istringstream stream(text);
    std::string raw;
    while (std::getline(stream, raw))
    {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        auto bracket = raw.find('[');
        while (bracket != std::string::npos)
        {
            const auto colon = raw.find(':', bracket + 1);
            const auto close = raw.find(']', bracket + 1);
            if (colon == std::string::npos || close == std::string::npos || colon > close
                || colon - bracket < 2 || colon - bracket > 4 || close - colon < 3)
            {
                break;
            }
            try
            {
                const auto minutes = std::stoi(raw.substr(bracket + 1, colon - bracket - 1));
                const auto secondsValue = std::stod(raw.substr(colon + 1, close - colon - 1));
                if (minutes >= 0 && secondsValue >= 0 && secondsValue < 60)
                {
                    const auto milliseconds = static_cast<long long>(minutes * 60000 + secondsValue * 1000.0);
                    auto lyric = Utf8ToWide(raw.substr(close + 1));
                    lines.push_back({std::chrono::milliseconds(milliseconds), Trim(std::move(lyric))});
                }
            }
            catch (...)
            {
            }
            bracket = raw.find('[', bracket + 1);
        }
    }
    std::sort(lines.begin(), lines.end(), [](const auto& left, const auto& right) { return left.position < right.position; });
    return lines;
}

std::wstring LyricsStore::Current(const std::vector<LyricLine>& lines, std::chrono::milliseconds elapsed)
{
    std::wstring current;
    for (const auto& line : lines)
    {
        if (line.position > elapsed) break;
        current = line.text;
    }
    return current;
}

std::vector<std::uint8_t> LoadCover(const std::wstring& url)
{
    auto encoded = Download(url);
    if (encoded.empty())
    {
        return {};
    }
    auto result = DecodeCover(encoded);
    if (result.empty())
    {
        Log(L"failed to decode cover image");
    }
    return result;
}
}
