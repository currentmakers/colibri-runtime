do
    local next_time = 0
    local state = 0
    local subscription1 = 0
    local rgb = events.RGB_SET
    local tick = events.TIME_PERIOD

    function init()
        publish(rgb, 0)
        subscription1 = subscribe(tick,0)
    end

    function event (event_type, event_parameter, value)
        if event_type == tick then
            if next_time < value then
                publish(rgb, -state)
                state = (state + 1) % 5
                next_time = value + 300
            end
        end
    end

    function terminating()
        publish(rgb, 0)
        unsubscribe(subscription1)
    end
end