# -*- coding: utf-8 -*-
from protopy.parser import BinParser, simple_enum
from subprocess import Popen, PIPE

import pkg_resources
import os


def generate_proto_binary(source, text, message='Test'):
    proto_roots = pkg_resources.resource_filename(
        __name__,
        './resources',
    )
    roots = [proto_roots]
    test_proto = pkg_resources.resource_filename(
        __name__,
        os.path.join('./resources/', source),
    )
    cmd = [
        'protoc',
        '--encode={}'.format(message),
        '-I={}'.format(proto_roots),
        test_proto,
    ]
    print('calling protoc with: {}'.format(cmd))
    protoc = Popen(
        cmd,
        stdout=PIPE,
        stderr=PIPE,
        stdin=PIPE,
    )
    stdout, stderr = protoc.communicate(input=text)
    if protoc.returncode:
        raise Exception(stderr.decode('utf-8'))

    return roots, test_proto, stdout


def test_gen_load_file():
    roots, test_proto, content = generate_proto_binary(
        'test.proto',
        b'test: 123\ntest_whatever: "123456"',
    )
    print('generated proto message: {}'.format(content))

    # for _ in range(100):
    #     result = BinParser(roots).parse(test_proto, 'Test', content)

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.test == 123


def try_gc():
    import gc
    gc.set_debug(gc.DEBUG_LEAK)
    gc.collect()

    for g in gc.garbage:
        print('gc.garbage: {}'.format(g))


def test_inner_message():
    roots, test_proto, content = generate_proto_binary(
        'test_embedded.proto',
        b'''test: 123
            first_inner: {test_fixed: 4567}
            second_inner: {test_long: 135, test_short: 689}
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.test == 123


def test_enum():
    roots, test_proto, content = generate_proto_binary(
        'test_enum.proto',
        b'''test_1: 33
            test_2: 2
            test_3: 0
            test_4: 5
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    TestEnum = type(result.test_1)
    assert result.test_1 == TestEnum['TEST_MEMBER_3']


def test_simple_enum():
    roots, test_proto, content = generate_proto_binary(
        'test_enum.proto',
        b'''test_1: 33
            test_2: 2
            test_3: 0
            test_4: 5
        ''',
    )
    print('generated proto message: {}'.format(content))

    parser = BinParser(roots)
    parser.def_parser.enum_ctor = simple_enum
    result = parser.parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.test_1 == 'TestEnum.TEST_MEMBER_3'


def test_repeated():
    roots, test_proto, content = generate_proto_binary(
        'test_repeated.proto',
        b'''simple_repeats: {
            some_ints: [1, -1, 2, -2, 3, -3, 4, -4, 5, -5]
        }
        multiple_repeats: [{
            some_fixed: [1, -1, 2, -2, 3, -3]
            some_strings: ["foo", "bar", "\x01weird\x10"]
        },
        {
            some_fixed: [4, -4, 5, -5, 6, -6]
        }]
        multiple_oneof: {
            some_fixed: [123456789, 987654321]
            some_strings: [""]
        }
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.either_or.some_fixed == [123456789, 987654321]


def test_fixed64():
    roots, test_proto, content = generate_proto_binary(
        'test_fixed64.proto',
        b'''sfixed_fixed: [{
            key: -1
            value: 123
        },
        {
            key: -1000
            value: 1000
        }]
        simple_fixed: 1010101010
        simple_sfixed: -1010101010
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.simple_fixed == 1010101010
    # try_gc()


def test_1_fixed64():
    roots, test_proto, content = generate_proto_binary(
        'test_fixed64.proto',
        b'''some_fixed: [1234, 12, 9876, 0]
        some_sfixed: [-9876, 9876, 0, -1, 1]
        sfixed_fixed: [{
            key: -1
            value: 1
        },
        {
            key: -1000
            value: 1000
        }]
        simple_fixed: 1010101010
        simple_sfixed: -1010101010
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.some_fixed[1] == 12


def test_map():
    roots, test_proto, content = generate_proto_binary(
        'test_map.proto',
        b'''simple_map: [{
                key: "foo"
                value: 1
            },
            {
                key: "bar"
                value: 2
            },
            {
                key: "baz"
                value: 3
            }]
            inner_map: [{
                key: 1
                value: {
                    sint_uint: [{
                        key: 1
                        value: 1
                    },
                    {
                        key: 2,
                        value: 2
                    },
                    {
                        key: 3
                        value: 3
                    }]
                }
            },
            {
                key: 2
                value: {
                    sint_uint: [{
                        key: -1
                        value: 1
                    },
                    {
                        key: -2,
                        value: 2
                    },
                    {
                        key: -3
                        value: 3
                    }]
                }
            },
            {
                key: 3
                value: {
                    sint_uint: [{
                        key: -1
                        value: 2
                    },
                    {
                        key: -2,
                        value: 4
                    },
                    {
                        key: -3
                        value: 6
                    }]
                }
            }]
        inner: {
            sint_uint: [{
                key: 1
                value: 1
            },
            {
                key: 2,
                value: 2
            },
            {
                key: 3
                value: 3
            }]
        }
        inner_inner: {
            bytes_inner_map: [{
                key: 123
                value: {
                    sint_uint: [{
                        key: -1
                        value: 4
                    },
                    {
                        key: -2,
                        value: 8
                    },
                    {
                        key: -3
                        value: 12
                    }]
                }
            },
            {
                key: 456
                value: {
                    sint_uint: [{
                        key: -1
                        value: 8
                    },
                    {
                        key: -2,
                        value: 16
                    },
                    {
                        key: -3
                        value: 24
                    }]
                }
            },
            {
                key: 789
                value: {
                    sint_uint: [{
                        key: 1
                        value: 16
                    },
                    {
                        key: 2,
                        value: 32
                    },
                    {
                        key: 3
                        value: 48
                    }]
                }
            }]
        }
        inner_inner_inner: {
            string_inner_inner_map: [{
                key: "foo"
                value: {
                    bytes_inner_map: [{
                        key: 12
                        value: {
                            sint_uint: [{
                                key: 1
                                value: 16
                            },
                            {
                                key: 2,
                                value: 32
                            },
                            {
                                key: 3
                                value: 48
                            }]
                        }
                    },
                    {
                        key: 34
                        value: {
                            sint_uint: [{
                                key: 1
                                value: 16
                            },
                            {
                                key: 2,
                                value: 32
                            },
                            {
                                key: 3
                                value: 48
                            }]
                        }
                    }]
                },
            },
            {
                key: "bar"
                value: {
                    bytes_inner_map: [{
                        key: 56
                        value: {
                            sint_uint: [{
                                key: 1
                                value: 16
                            },
                            {
                                key: 2,
                                value: 32
                            },
                            {
                                key: 3
                                value: 48
                            }]
                        }
                    },
                    {
                        key: 78
                        value: {
                            sint_uint: [{
                                key: 1
                                value: 16
                            },
                            {
                                key: 2,
                                value: 32
                            },
                            {
                                key: 3
                                value: 48
                            }]
                        }
                    }]
                }
            }]
        }
        ''',
    )
    print('generated proto message: {}'.format(content))

    result = BinParser(roots).parse(test_proto, 'Test', content)

    print('result: {}'.format(result))
    assert result.inner_inner_inner.string_inner_inner_map['bar'] \
                                   .bytes_inner_map[78].sint_uint[2] == 32


def test_invalid_source_path():
    roots = [pkg_resources.resource_filename(
        __name__,
        './resources',
    )]
    try:
        BinParser(roots).parse(None, 'Test', 'does not matter')
    except FileNotFoundError:
        pass
    roots, test_proto, content = generate_proto_binary(
        'test.proto',
        b'test: 123\ntest_whatever: "123456"',
    )
    print('generated proto message: {}'.format(content))

    parser = BinParser(roots)
    parser.parse(test_proto, 'Test', content)
    try:
        parser.parse(None, 'Test', content)
    except FileNotFoundError:
        pass
