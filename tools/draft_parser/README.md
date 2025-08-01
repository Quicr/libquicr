## Media Over Quic Transport (MOQT) Draft: Parser / Code Generator

This is a simple parser and code generator for the Media Over Quic Transport Draft. It is written in Python and uses regular expressions to parse the draft. The code is generated using Jinja2 templates.

## Running the Parser

The parser is integrated into the libquicr build directly. Generally, you should not need to run this tool manually as a consumer of this library.

To run the parser, you will need to have Python 3 installed. The requirements.txt lists any required modules. To install you will probably want to create a virtual environment and install the requirements:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

You can run the parser by executing the following command:

```bash
python3 main.py ./drafts/moq_transport_draft_v8.txt ctrl_messages
```

The first argument is the path to the draft file, and the second argument is the name of the output source file names (without the extension). The parser will generate a {source}.cpp and {source.h} files in the current directory.

## Parsing and Code Generation

### Draft Requirements

The parser searches for MOQT controll messages. These messages begin with text that is followed by "Message {" and end with "}". The parser will extract the message name and the fields in the message. All message names and field names are expected to be text words beginning with a capital letter.

### Example: CLIENT_SETUP Message

The following is an example of a CLIENT_SETUP message in the draft:

```
   CLIENT_SETUP Message {
     Type (i) = 0x40,
     Length (i),
     Number of Supported Versions (i),
     Supported Versions (i) ...,
     Number of Parameters (i),
     Setup Parameters (..) ...,
   }
```

The parser reads this message from the draft and gerates a MessageSpec object that contains information about the message fields. The code gerator uses thie MessageSpec object with Jinja2 templates to generate the C++ code for the message.

For the CLIENT_SETUP message the C++ code generated is as follows:

Header:
```cpp
    struct ClientSetup
    {
        SupportedVersions supported_versions;
        SetupParameters setup_parameters;
    };
```

Source:
```cpp
    Bytes& operator<<(Bytes& buffer, const ClientSetup& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.supported_versions; // (i) ... << SupportedVersions
        payload << msg.setup_parameters; // (..) ... << SetupParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kClientSetup);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg)
    {
        buffer = buffer >> msg.supported_versions; // (i) ... >> SupportedVersions
        buffer = buffer >> msg.setup_parameters; // (..) ... >> SetupParameters
        return buffer;
    }
```

For this project the Type and Length fields are ignored since they are decoded in a different way. Also, since both Supported Version and Setup Parameters are defined as repeatable (...) the code generator automatically generates these fields as vectors that automatically include streaming length fields.So, the "Number of..." fields are ignored.

The current project uses streaming for all encoding and decoding of MOQT control messages. This is reflected in the code generator as stream operators for the broader message as well as each of the message fields.

### Optional Fields

The parser supports parsing optional fiels in the draft. These fields are defined where fields are enclosed in square brackets. For example:

```
   SUBSCRIBE Message {
     Type (i) = 0x3,
     Length (i),
     Subscribe ID (i),
     Track Alias (i),
     Track Namespace (tuple),
     Track Name Length (i),
     Track Name (..),
     Subscriber Priority (8),
     Group Order (8),
     Filter Type (i),
     [Start Group (i),
      Start Object (i)],
     [End Group (i)],
     Number of Parameters (i),
     Subscribe Parameters (..) ...
   }
```

Here is the generated header file definition for the SUBSCRIBE message:

```cpp
    struct Subscribe
    {
        struct Group_0 {
            StartGroup start_group;
            StartObject start_object;
        };
        struct Group_1 {
            EndGroup end_group;
        };
        SubscribeID subscribe_id;
        TrackAlias track_alias;
        TrackNamespace track_namespace;
        TrackName track_name;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FilterType filter_type;
        std::function<void (Subscribe&)> optional_group_0_cb;
        std::optional<Subscribe::Group_0> group_0;
        std::function<oid (Subscribe&)>optional_group_1_cb;
        std::optional<Subscribe::Group_1> group_1;
        SubscribeParameters subscribe_parameters;
    };
```

Notice how the optional fields from the original specification definition are mapped to nested structures. These nested structures are then uses as types for `std::optional` fields in the main message structure. Also note that the code generator also includes `std::function` fields. These fields should be set to callback methods to determine when and if optional fields are present in the message.


## Future Work

There is still work to be done. The MOQT draft is still in development and the parser and code generator will need to be updated as the draft changes. Also, the parser and code generator could be improved to handle more complex message structures and to generate more efficient code.