package Lucy;
use strict;
use warnings;

use 5.008003;

our $VERSION = 0.01;
$VERSION = eval $VERSION;

use XSLoader;
BEGIN { XSLoader::load( 'Lucy', '0.01' ) }

use Lucy::Autobinding;

sub error {$Lucy::Object::Err::error}

{
    package Lucy::Util::ToolSet;
    use Carp qw( carp croak cluck confess );
    use Scalar::Util qw( blessed );
    use Storable qw( nfreeze thaw );

    BEGIN {
        push our @ISA, 'Exporter';
        our @EXPORT_OK = qw(
            carp
            croak
            cluck
            confess
            blessed
            nfreeze
            thaw
            to_lucy
            to_perl
        );
    }
}

{
    package Lucy::Object::CharBuf;

    {
        # Defeat obscure bugs in the XS auto-generation by redefining clone()
        # and deserialize().  (Because of how the typemap works for CharBuf*,
        # the auto-generated methods return UTF-8 Perl scalars rather than
        # actual CharBuf objects.)
        no warnings 'redefine';
        sub clone       { shift->_clone(@_) }
        sub deserialize { shift->_deserialize(@_) }
    }

    package Lucy::Object::ViewCharBuf;
    use Lucy::Util::ToolSet qw( confess );

    sub new { confess "ViewCharBuf has no public constructor." }

    package Lucy::Object::ZombieCharBuf;
    use Lucy::Util::ToolSet qw( confess );

    sub new { confess "ZombieCharBuf objects can only be created from C." }

    sub DESTROY { }
}

{
    package Lucy::Object::Err;
    use Lucy::Util::ToolSet qw( blessed );

    sub do_to_string { shift->to_string }
    use Carp qw( longmess );
    use overload
        '""'     => \&do_to_string,
        fallback => 1;

    sub new {
        my ( $either, $message ) = @_;
        my ( undef, $file, $line ) = caller;
        $message .= ", $file line $line\n";
        return $either->_new(
            mess => Lucy::Object::CharBuf->new($message) );
    }

    sub do_throw {
        my $err = shift;
        $err->cat_mess( longmess() );
        die $err;
    }

    our $error;
    sub set_error {
        my $val = $_[1];
        if ( defined $val ) {
            confess("Not a Lucy::Object::Err")
                unless ( blessed($val)
                && $val->isa("Lucy::Object::Err") );
        }
        $error = $val;
    }
    sub get_error {$error}
}

{
    package Lucy::Object::Obj;
    use Lucy::Util::ToolSet qw( to_lucy to_perl );
    sub load { return $_[0]->_load( to_lucy( $_[1] ) ) }
}

{
    package Lucy::Object::VArray;
    no warnings 'redefine';
    sub clone { CORE::shift->_clone }
}

{
    package Lucy::Object::VTable;

    sub find_parent_class {
        my ( undef, $package ) = @_;
        no strict 'refs';
        for my $parent ( @{"$package\::ISA"} ) {
            return $parent if $parent->isa('Lucy::Object::Obj');
        }
        return;
    }

    sub novel_host_methods {
        my ( undef, $package ) = @_;
        no strict 'refs';
        my $stash = \%{"$package\::"};
        my $methods
            = Lucy::Object::VArray->new( capacity => scalar keys %$stash );
        while ( my ( $symbol, $glob ) = each %$stash ) {
            next if ref $glob;
            next unless *$glob{CODE};
            $methods->push( Lucy::Object::CharBuf->new($symbol) );
        }
        return $methods;
    }

    sub _register {
        my ( undef, %args ) = @_;
        my $singleton_class = $args{singleton}->get_name;
        my $parent_class    = $args{parent}->get_name;
        if ( !$singleton_class->isa($parent_class) ) {
            no strict 'refs';
            push @{"$singleton_class\::ISA"}, $parent_class;
        }
    }
}

{
    package Lucy::Util::Json;
    use Lucy::Util::ToolSet qw( to_lucy );

    use JSON::XS qw();

    my $json_encoder = JSON::XS->new->pretty(1)->canonical(1);

    sub slurp_json {
        my ( undef, %args ) = @_;
        my $instream = $args{folder}->open_in( $args{path} )
            or return;
        my $len = $instream->length;
        my $json;
        $instream->read( $json, $len );
        my $result = eval { to_lucy( $json_encoder->decode($json) ) };
        if ( $@ or !$result ) {
            Lucy::Object::Err->set_error(
                Lucy::Object::Err->new( $@ || "Failed to decode JSON" )
            );
            return;
        }
        return $result;
    }

    sub spew_json {
        my ( undef, %args ) = @_;
        my $json = eval { $json_encoder->encode( $args{'dump'} ) };
        if ( !defined $json ) {
            Lucy::Object::Err->set_error(
                Lucy::Object::Err->new($@) );
            return 0;
        }
        my $outstream = $args{folder}->open_out( $args{path} );
        return 0 unless $outstream;
        $outstream->print($json);
        eval { $outstream->close; };
        return 0 if $@;
        return 1;
    }

    sub to_json {
        my ( undef, $dump ) = @_;
        return $json_encoder->encode($dump);
    }

    sub from_json {
        return to_lucy( $json_encoder->decode( $_[1] ) );
    }
}

1;

__END__

__BINDING__

my $lucy_xs_code = <<'END_XS_CODE';
MODULE = Lucy    PACKAGE = Lucy 

BOOT:
    lucy_Lucy_bootstrap();

IV
_dummy_function()
CODE:
    RETVAL = 1;
OUTPUT:
    RETVAL
END_XS_CODE

my $toolset_xs_code = <<'END_XS_CODE';
MODULE = Lucy    PACKAGE = Lucy::Util::ToolSet

SV*
to_lucy(sv)
    SV *sv;
CODE:
{
    lucy_Obj *obj = XSBind_perl_to_lucy(sv);
    RETVAL = LUCY_OBJ_TO_SV_NOINC(obj);
}
OUTPUT: RETVAL

SV*
to_perl(sv)
    SV *sv;
CODE:
{
    if (sv_isobject(sv) && sv_derived_from(sv, "Lucy::Object::Obj")) {
        IV tmp = SvIV(SvRV(sv));
        lucy_Obj* obj = INT2PTR(lucy_Obj*, tmp);
        RETVAL = XSBind_lucy_to_perl(obj);
    }
    else {
        RETVAL = newSVsv(sv);
    }
}
OUTPUT: RETVAL
END_XS_CODE

Boilerplater::Binding::Perl::Class->register(
    parcel     => "Lucy",
    class_name => "Lucy::Util::Toolset",
    xs_code    => $toolset_xs_code,
);

Boilerplater::Binding::Perl::Class->register(
    parcel     => "Lucy",
    class_name => "Lucy",
    xs_code    => $lucy_xs_code,
);

__POD__

=head1 NAME

Lucy - Search engine library.

=head1 SYNOPSIS

To do.

=head1 DESCRIPTION

To do.

=head1 AUTHOR

Marvin Humphrey E<lt>marvin at rectangular dot comE<gt>

=head1 COPYRIGHT AND LICENSE

    /**
     * Copyright 2009 The Apache Software Foundation
     *
     * Licensed under the Apache License, Version 2.0 (the "License");
     * you may not use this file except in compliance with the License.
     * You may obtain a copy of the License at
     *
     *     http://www.apache.org/licenses/LICENSE-2.0
     *
     * Unless required by applicable law or agreed to in writing, software
     * distributed under the License is distributed on an "AS IS" BASIS,
     * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
     * implied.  See the License for the specific language governing
     * permissions and limitations under the License.
     */

=cut

