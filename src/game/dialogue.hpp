#pragma once
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

namespace gp
{

    // Replaces "%s" in order. Never pass mod-supplied text to printf as a format.
    inline std::string fmtS(std::string f, std::initializer_list<std::string> args)
    {
        size_t pos = 0;
        for (const auto &a : args)
        {
            pos = f.find("%s", pos);
            if (pos == std::string::npos)
                break;
            f.replace(pos, 2, a);
            pos += a.size();
        }
        return f;
    }

    // Replaces EVERY "%s" with the same text (for lines that use the pet's name more than once).
    inline std::string fmtAll(std::string f, const std::string &a)
    {
        for (size_t pos = 0; (pos = f.find("%s", pos)) != std::string::npos; pos += a.size())
            f.replace(pos, 2, a);
        return f;
    }

    enum class BoxKind
    {
        Message = 0,
        Confirm = 1,
        Centered = 2,
        Quantity = 3,
        StatsLow = 4,
        Stats = 5,
        Picture = 6,
        Quest = 7,
        Sign = 8
    };

    struct DialogueBox
    {
        BoxKind kind = BoxKind::Message;
        std::string title, body;
        std::string npcId;        // Stats / StatsLow: whose requirements are shown
        std::array<int, 4> req{}; // halos, pitchforks, stars, lightbulbs (filled when the box opens)
        std::function<void()> onYes;
        std::function<void()> onNo;
        std::string portrait;    // UI sprite id (uiSpriteId(tile, palette)); empty = none
        int layout = 2;          // 0, 1 = small windows, 2 = stats window
        bool yesSelected = true; // flag 0x12: cursor on Yes (the default is a guess)
        int qty = 1, qtyMax = 1;
        std::function<void(int)> onQuantity;
    };

    // PrepareDialogueMessage: the layout comes from the text length (formatted text, terminator included).
    inline DialogueBox messageBox(std::string body)
    {
        DialogueBox b;
        b.kind = BoxKind::Message;
        const size_t n = body.size() + 1; // ASCII assumed
        b.layout = n <= 0x31 ? 0 : n <= 0x63 ? 1
                                             : 2;
        b.body = std::move(body);
        return b;
    }

    struct Dialogue
    {
        std::optional<DialogueBox> box;
        bool open() const { return box.has_value(); }
        void show(DialogueBox b) { box = std::move(b); }
        // option 1 = yes/OK, 2 = no. Closes first, so a callback may open the next box.
        void choose(int option)
        {
            if (!box)
                return;
            DialogueBox b = std::move(*box);
            box.reset();
            auto &cb = (option == 2) ? b.onNo : b.onYes;
            if (cb)
                cb();
        }
        void pick(int qty)
        { // closes first, like choose()
            if (!box)
                return;
            DialogueBox b = std::move(*box);
            box.reset();
            if (b.onQuantity)
                b.onQuantity(qty);
        }
    };

} // namespace gp