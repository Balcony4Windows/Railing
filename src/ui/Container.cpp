#include "balcony/ui/Container.h"

#include <algorithm>

namespace balcony::ui
{
    void Container::Add(Component* component)
    {
        if (component)
            _children.push_back(component);
    }

    void Container::Remove(Component* component)
    {
        _children.erase(std::remove(_children.begin(), _children.end(), component), _children.end());
    }

    void Container::Update(float deltaSeconds)
    {
        for (Component* child : _children)
            child->Update(deltaSeconds);
    }

    void Container::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        VisualComponent::Draw(renderer);

        for (const Component* child : _children)
            child->Draw(renderer);
    }

    Component* Container::FindHit(float x, float y)
    {
        for (auto it = _children.rbegin(); it != _children.rend(); ++it)
        {
            if (Component* hit = (*it)->FindHit(x, y))
                return hit;
        }

        return HitTest(x, y) ? this : nullptr;
    }

    Component* Container::FindDraggable(float x, float y)
    {
        for (auto it = _children.rbegin(); it != _children.rend(); ++it)
        {
            if (Component* hit = (*it)->FindDraggable(x, y))
                return hit;
        }

        return Component::FindDraggable(x, y);
    }

    void Container::Translate(float dx, float dy)
    {
        VisualComponent::Translate(dx, dy);
        for (Component* child : _children)
            child->Translate(dx, dy);
    }
}
