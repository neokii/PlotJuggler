#include "introspection_parser.h"
#include "dialog_with_itemlist.h"

double IntrospectionParser::extractRealValue(
        const RosIntrospection::Variant& value,
        const std::string& item_name)
{
    if( value.getTypeID() == RosIntrospection::UINT64)
    {
        uint64_t val_i = value.extract<uint64_t>();
        double val_d = static_cast<double>(val_i);
        bool error = (val_i != static_cast<uint64_t>(val_d));
        if(error && _warnings_enabled)
        {
            _warn_cancellation.insert( item_name );
        }
        return val_d;
    }

    if( value.getTypeID() == RosIntrospection::INT64)
    {
        int64_t val_i = value.extract<int64_t>();
        double val_d = static_cast<double>(val_i);
        bool error = (val_i != static_cast<int64_t>(val_d));
        if(error && _warnings_enabled)
        {
            _warn_cancellation.insert( item_name );
        }
        return val_d;
    }

    double val_d = value.convert<double>();
    return val_d;
}

void IntrospectionParser::setMaxArrayPolicy(size_t max_array_size, bool discard_entire_array)
{
    _max_array_size = max_array_size;
    _discard_large_array = discard_entire_array;
    _ros_parser.setMaxArrayPolicy( discard_entire_array );
}

void IntrospectionParser::pushRawMessage(const MessageKey &topic_name,
                                         const RawMessage &msg,
                                         double timestamp)
{
    using namespace RosIntrospection;

    FlatMessage flat_container;
    RenamedValues renamed_values;

    absl::Span<uint8_t> msg_view ( const_cast<uint8_t*>(msg.data()), msg.size());

    bool max_size_ok = _ros_parser.deserializeIntoFlatContainer(
                topic_name,
                msg_view,
                &flat_container,
                _max_array_size );

    if( !max_size_ok && _warnings_enabled )
    {
        _warn_max_arraysize.insert(topic_name);
    }

    _ros_parser.applyNameTransform( topic_name,
                                    flat_container,
                                    &renamed_values );   
// TODO
//        if(use_header_stamp)
//        {
//            const auto header_stamp = FlatContainerContainHeaderStamp(flat_container);
//            if(header_stamp)
//            {
//                const double time = header_stamp.value();
//                if( time > 0 ) {
//                    msg_time = time;
//                }
//                else{
//                    warning_headerstamp.insert(topic_name);
//                }
//            }
//        }

    for(const auto& it: renamed_values )
    {
        const auto& field_name = it.first;

        const RosIntrospection::Variant& value = it.second;

        auto plot_pair = _plot_map.numeric.find( field_name );
        if( (plot_pair == _plot_map.numeric.end()) )
        {
            plot_pair = _plot_map.addNumeric( field_name );
        }

        PlotData& plot_data = plot_pair->second;
        size_t data_size = plot_data.size();
        if( data_size > 0 )
        {
            const double last_time = plot_data.back().x;
            if( timestamp < last_time)
            {
                _warn_monotonic.insert( field_name);
            }
        }

        double val_d = extractRealValue(value , field_name);
        plot_data.pushBack( PlotData::Point(timestamp, val_d) );
    }

}

void IntrospectionParser::showWarnings()
{
    if( !_warn_max_arraysize.empty() )
    {
        QString message = QString("The following topics contain arrays with more than %1 elements.\n").arg(_max_array_size);
        if( _discard_large_array )
        {
            message += QString("The fields containing the extra large arrays have been discarded\n");
        }
        else{
            message += QString("These arrays were trunkated to the maximum size %1\n").arg(_max_array_size);
        }
        DialogWithItemList::warning( message, _warn_max_arraysize );
    }

    if( !_warn_monotonic.empty() )
    {
        QString message = "The time of one or more fields is not strictly monotonic.\n"
                          "Some plots will not be displayed correctly\n";

        DialogWithItemList::warning( message, _warn_monotonic );
    }

    if( !_warn_headerstamp.empty() )
    {
        QString message = "You checked the option:\n\n"
                          "[If present, use the timestamp in the field header.stamp]\n\n"
                          "But the [header.stamp] of one or more messages were NOT initialized correctly.\n";
        DialogWithItemList::warning( message, _warn_headerstamp );
    }

    if( !_warn_cancellation.empty() )
    {
        QString message = "During the parsing process, one or more conversions to double failed"
                          " because of numerical cancellation.\n"
                          "This happens when the absolute value of a long integer exceed 2^52.\n\n"
                          "You have been warned... don't trust the following timeseries\n";
        DialogWithItemList::warning( message, _warn_cancellation );
    }
}

