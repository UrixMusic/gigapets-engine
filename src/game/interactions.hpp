// interactions.hpp - "what happens when you press Z on this object". Handlers are registered by name;
// content (the base game or a mod) picks one with InteractionDef::action.
#pragma once
#include "../content/records.hpp"
#include <functional>
#include <map>
#include <string>

namespace gp
{
    class InteractionRegistry
    {
    public:
        using Handler = std::function<void(const InteractionDef &)>;
        void add(const std::string &action, Handler h) { handlers_[action] = std::move(h); }
        bool run(const InteractionDef &d) const
        {
            auto i = handlers_.find(d.action);
            if (i == handlers_.end())
                return false;
            i->second(d);
            return true;
        }

    private:
        std::map<std::string, Handler> handlers_;
    };
} // namespace gp