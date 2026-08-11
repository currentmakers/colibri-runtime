do
    local next_time = 0
    local state = 0
    local subscription1 = 0
    local rgb = events.RGB_SET
    local tick = events.TIME_PERIOD

    function event (event_type, event_parameter, value)
        if event_type == tick then
            if next_time < value then
                publish(rgb, 0, -state)
                state = (state + 1) % 8
                next_time = value + 500
            end
        end
    end

    function terminating()
        publish(rgb, 0, 0)
        unsubscribe(subscription1)
    end

    publish(rgb, 0, 0x04)
    subscription1 = subscribe(tick,100)  -- Event every 10ms. Supported times, 1, 10, 100, 1000 ms
end
