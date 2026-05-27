do
    local next=0
    local state=0

    local function tick()
        set_rgb_color(-state)
        state = (state + 1) % 5
    end

    function init()
        set_rgb_color(0)
    end

    function event (event_id, value)
        if next < value then
            tick()
            next = now + 300
        end
    end

    function terminating()
        set_rgb_color(0)
    end
end